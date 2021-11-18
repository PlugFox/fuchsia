// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <lib/boot-options/boot-options.h>
#include <lib/zbitl/error_stdio.h>
#include <lib/zbitl/image.h>
#include <lib/zbitl/items/mem_config.h>
#include <lib/zbitl/memory.h>
#include <lib/zircon-internal/macros.h>

#include <cstddef>

#include <arch/mp.h>
#include <arch/ops.h>
#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <arch/x86/mmu.h>
#include <arch/x86/pv.h>
#include <fbl/array.h>
#include <kernel/cpu_distance_map.h>
#include <ktl/algorithm.h>
#include <phys/handoff.h>

#include "platform_p.h"
#if defined(WITH_KERNEL_PCIE)
#include <dev/pcie_bus_driver.h>
#endif
#include <lib/cksum.h>
#include <lib/debuglog.h>
#include <lib/lazy_init/lazy_init.h>
#include <lib/system-topology.h>
#include <mexec.h>
#include <platform.h>
#include <string.h>
#include <trace.h>
#include <zircon/boot/driver-config.h>
#include <zircon/boot/e820.h>
#include <zircon/boot/image.h>
#include <zircon/errors.h>
#include <zircon/pixelformat.h>
#include <zircon/types.h>

#include <dev/uart.h>
#include <explicit-memory/bytes.h>
#include <fbl/alloc_checker.h>
#include <fbl/vector.h>
#include <kernel/cpu.h>
#include <lk/init.h>
#include <platform/console.h>
#include <platform/crashlog.h>
#include <platform/keyboard.h>
#include <platform/pc.h>
#include <platform/pc/acpi.h>
#include <platform/pc/bootloader.h>
#include <platform/pc/efi.h>
#include <platform/pc/efi_crashlog.h>
#include <platform/pc/smbios.h>
#include <platform/ram_mappable_crashlog.h>
#include <vm/bootalloc.h>
#include <vm/bootreserve.h>
#include <vm/physmap.h>
#include <vm/pmm.h>
#include <vm/vm_aspace.h>

#define LOCAL_TRACE 0

pc_bootloader_info_t bootloader;

namespace {
namespace crashlog_impls {
lazy_init::LazyInit<RamMappableCrashlog, lazy_init::CheckType::None,
                    lazy_init::Destructor::Disabled>
    ram_mappable;
EfiCrashlog efi;
}  // namespace crashlog_impls
}  // namespace

// convert from legacy format
static unsigned pixel_format_fixup(unsigned pf) {
  switch (pf) {
    case 1:
      return ZX_PIXEL_FORMAT_RGB_565;
    case 2:
      return ZX_PIXEL_FORMAT_RGB_332;
    case 3:
      return ZX_PIXEL_FORMAT_RGB_2220;
    case 4:
      return ZX_PIXEL_FORMAT_ARGB_8888;
    case 5:
      return ZX_PIXEL_FORMAT_RGB_x888;
    default:
      return pf;
  }
}

static bool early_console_disabled;

// Copy ranges in the given ZBI into a newly-allocated array of zbi_mem_range_t structs.
//
// Allocation takes place from early booth memory, which cannot be released.
static ktl::span<zbi_mem_range_t> get_memory_ranges(ktl::span<std::byte> zbi) {
  zbitl::View zbi_view(zbitl::ByteView(zbi.data(), zbi.size()));
  fitx::result<std::string_view, zbitl::MemRangeTable> range_table =
      zbitl::MemRangeTable::FromView(zbi_view);
  if (range_table.is_error()) {
    panic("Failed to find memory information in ZBI: %*s\n",
          static_cast<int>(range_table.error_value().size()), range_table.error_value().data());
  }

  // Allocate memory for the ranges.
  size_t num_ranges = range_table->size();
  zbi_mem_range_t* ranges =
      reinterpret_cast<zbi_mem_range_t*>(boot_alloc_mem(sizeof(zbi_mem_range_t) * num_ranges));
  ZX_ASSERT(ranges != nullptr);

  // Itereate over the the range table (which converts the various memory range formats into
  // zbi_mem_range_t), and make a copy.
  size_t n = 0;
  for (const zbi_mem_range_t& range : *range_table) {
    ranges[n++] = range;
  }
  ZX_ASSERT(n == num_ranges);
  return {ranges, num_ranges};
}

static void platform_save_bootloader_data(void) {
  if (gPhysHandoff->arch_handoff.acpi_rsdp) {
    bootloader.acpi_rsdp = gPhysHandoff->arch_handoff.acpi_rsdp.value();
  }

  // Handle individual ZBI items.
  ktl::span<ktl::byte> zbi = ZbiInPhysmap();
  zbitl::View view(zbi);
  for (auto it = view.begin(); it != view.end(); ++it) {
    auto [header, payload] = *it;
    switch (header->type) {
      case ZBI_TYPE_SMBIOS: {
        if (payload.size() >= sizeof(uint64_t)) {
          bootloader.smbios = *reinterpret_cast<uint64_t*>(payload.data());
        }
        break;
      }
      case ZBI_TYPE_EFI_SYSTEM_TABLE: {
        if (payload.size() >= sizeof(uint64_t)) {
          bootloader.efi_system_table =
              reinterpret_cast<void*>(*reinterpret_cast<uint64_t*>(payload.data()));
        }
        break;
      }
      case ZBI_TYPE_FRAMEBUFFER: {
        if (payload.size() >= sizeof(zbi_swfb_t)) {
          memcpy(&bootloader.fb, payload.data(), sizeof(zbi_swfb_t));
        }
        bootloader.fb.format = pixel_format_fixup(bootloader.fb.format);
        break;
      }
      case ZBI_TYPE_CMDLINE: {
        if (payload.empty()) {
          break;
        }
        payload.back() = ktl::byte{'\0'};
        ParseBootOptions(
            ktl::string_view{reinterpret_cast<const char*>(payload.data()), payload.size()});

        // The CMDLINE might include entropy for the zircon cprng.
        // We don't want that information to be accesible after it has
        // been added to the kernel cmdline.
        // Editing the header of a ktl::span will not result in an error.
        static_cast<void>(view.EditHeader(it, zbi_header_t{.type = ZBI_TYPE_DISCARD}));
        mandatory_memset(payload.data(), 0, payload.size());
        break;
      }
      case ZBI_TYPE_NVRAM_DEPRECATED:
      case ZBI_TYPE_NVRAM: {
        // If we have a valid NVRAM location passed to us by ZBI, and we have
        // not already configured a platform crashlog implementation, use the
        // NVRAM location to back a RamMappableCrashlog implementation and
        // configure the generic platform layer to use it.
        if ((payload.size() >= sizeof(zbi_nvram_t)) && !PlatformCrashlog::HasNonTrivialImpl()) {
          zbi_nvram_t info;
          memcpy(&info, payload.data(), sizeof(info));
          crashlog_impls::ram_mappable.Initialize(info.base, info.length);
          PlatformCrashlog::Bind(crashlog_impls::ram_mappable.Get());
        }
        break;
      }
      case ZBI_TYPE_KERNEL_DRIVER: {
        switch (header->extra) {
          case KDRV_I8250_PIO_UART: {
            if (payload.size() >= sizeof(dcfg_simple_pio_t)) {
              dcfg_simple_pio_t pio;
              memcpy(&pio, payload.data(), sizeof(pio));
              bootloader.uart = pio;
            }
            break;
          }
          case KDRV_I8250_MMIO_UART: {
            if (payload.size() >= sizeof(dcfg_simple_t)) {
              dcfg_simple_t mmio;
              memcpy(&mmio, payload.data(), sizeof(mmio));
              bootloader.uart = mmio;
            }
            break;
          }
        };
        break;
      }
      case ZBI_TYPE_CRASHLOG: {
        crashlog_impls::efi.SetLastCrashlogLocation(
            {reinterpret_cast<char*>(payload.data()), payload.size()});
        break;
      }
      case ZBI_TYPE_DISCARD:
        break;
      case ZBI_TYPE_HW_REBOOT_REASON: {
        if (payload.size() >= sizeof(zbi_hw_reboot_reason_t)) {
          zbi_hw_reboot_reason_t reason;
          memcpy(&reason, payload.data(), sizeof(reason));
          platform_set_hw_reboot_reason(reason);
        }
        break;
      }
    };
  }
  if (auto result = view.take_error(); result.is_error()) {
    printf("process_zbi: error occurred during iteration: ");
    zbitl::PrintViewError(result.error_value());
    return;
  }

  // Prevent the early boot allocator from handing out the memory the ZBI data
  // is located in.
  auto phys = reinterpret_cast<uintptr_t>(view.storage().data());
  boot_alloc_reserve(phys, view.size_bytes());

  // Save memory range information from the ZBI.
  bootloader.memory_ranges = get_memory_ranges(zbi);
}

static void boot_reserve_zbi() {
  ktl::span zbi = ZbiInPhysmap();
  boot_reserve_add_range(physmap_to_paddr(zbi.data()), ROUNDUP_PAGE_SIZE(zbi.size_bytes()));
}

#include <lib/gfxconsole.h>

#include <dev/display.h>

zx_status_t display_get_info(struct display_info* info) {
  return gfxconsole_display_get_info(info);
}

bool platform_early_console_enabled() { return !early_console_disabled; }

static void platform_early_display_init(void) {
  struct display_info info;
  void* bits;

  if (bootloader.fb.base == 0) {
    return;
  }

  if (!gBootOptions->gfx_console_early) {
    early_console_disabled = true;
    return;
  }

  // allocate an offscreen buffer of worst-case size, page aligned
  bits = boot_alloc_mem(8192 + bootloader.fb.height * bootloader.fb.stride * 4);
  bits = (void*)((((uintptr_t)bits) + 4095) & (~4095));

  memset(&info, 0, sizeof(info));
  info.format = bootloader.fb.format;
  info.width = bootloader.fb.width;
  info.height = bootloader.fb.height;
  info.stride = bootloader.fb.stride;
  info.flags = DISPLAY_FLAG_HW_FRAMEBUFFER;
  info.framebuffer = (void*)X86_PHYS_TO_VIRT(bootloader.fb.base);

  gfxconsole_bind_display(&info, bits);
}

/* Ensure the framebuffer is write-combining as soon as we have the VMM.
 * Some system firmware has the MTRRs for the framebuffer set to Uncached.
 * Since dealing with MTRRs is rather complicated, we wait for the VMM to
 * come up so we can use PAT to manage the memory types. */
static void platform_ensure_display_memtype(uint level) {
  if (bootloader.fb.base == 0) {
    return;
  }
  if (early_console_disabled) {
    return;
  }
  struct display_info info;
  memset(&info, 0, sizeof(info));
  info.format = bootloader.fb.format;
  info.width = bootloader.fb.width;
  info.height = bootloader.fb.height;
  info.stride = bootloader.fb.stride;
  info.flags = DISPLAY_FLAG_HW_FRAMEBUFFER;

  void* addr = NULL;
  zx_status_t status = VmAspace::kernel_aspace()->AllocPhysical(
      "boot_fb", ROUNDUP(info.stride * info.height * 4, PAGE_SIZE), &addr, PAGE_SIZE_SHIFT,
      bootloader.fb.base, 0 /* vmm flags */,
      ARCH_MMU_FLAG_WRITE_COMBINING | ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE);
  if (status != ZX_OK) {
    TRACEF("Failed to map boot_fb: %d\n", status);
    return;
  }

  info.framebuffer = addr;
  gfxconsole_bind_display(&info, NULL);
}
LK_INIT_HOOK(display_memtype, &platform_ensure_display_memtype, LK_INIT_LEVEL_VM + 1)

void platform_init_crashlog(void) {
  // Nothing to do if we have already selected a crashlog implementation.
  if (PlatformCrashlog::HasNonTrivialImpl()) {
    return;
  }

  // Attempt to initialize EFI.
  zx_status_t result = InitEfiServices();
  if (result != ZX_OK) {
    dprintf(INFO, "No EFI available on system.\n");
    return;
  }

  // Initialize and select the EfiCrashlog implementation.
  PlatformCrashlog::Bind(crashlog_impls::efi);
}

zx_status_t platform_append_mexec_data(ktl::span<ktl::byte> data_zbi) {
  zbitl::Image image(data_zbi);
  // The only possible storage error that can result from a span-backed Image
  // would be a failure to increase the capacity.
  auto error = [](const auto& image_error) -> zx_status_t {
    return image_error.storage_error ? ZX_ERR_BUFFER_TOO_SMALL : ZX_ERR_INTERNAL;
  };

  // Append physical memory ranges.
  if (!bootloader.memory_ranges.empty()) {
    if (auto result = image.Append(zbi_header_t{.type = ZBI_TYPE_MEM_CONFIG},
                                   zbitl::AsBytes(bootloader.memory_ranges));
        result.is_error()) {
      printf("mexec: failed to append memory range metadata to data ZBI: ");
      zbitl::PrintViewError(result.error_value());
      return error(result.error_value());
    }
  }

  // Append information about the framebuffer to the data ZBI.
  if (bootloader.fb.base) {
    auto result =
        image.Append(zbi_header_t{.type = ZBI_TYPE_FRAMEBUFFER}, zbitl::AsBytes(bootloader.fb));
    if (result.is_error()) {
      printf("mexec: failed to append framebuffer data to data ZBI: ");
      zbitl::PrintViewError(result.error_value());
      return error(result.error_value());
    }
  }

  if (bootloader.efi_system_table) {
    auto result = image.Append(zbi_header_t{.type = ZBI_TYPE_EFI_SYSTEM_TABLE},
                               zbitl::AsBytes(bootloader.efi_system_table));
    if (result.is_error()) {
      printf("mexec: Failed to append EFI sys table data to data ZBI: ");
      zbitl::PrintViewError(result.error_value());
      return error(result.error_value());
    }
  }

  if (bootloader.smbios) {
    auto result =
        image.Append(zbi_header_t{.type = ZBI_TYPE_SMBIOS}, zbitl::AsBytes(bootloader.smbios));
    if (result.is_error()) {
      printf("mexec: failed to append SMBIOSs data to data ZBI: ");
      zbitl::PrintViewError(result.error_value());
      return error(result.error_value());
    }
  }

  auto add_uart = [&](uint32_t extra, auto bytes) -> zx_status_t {
    auto result = image.Append(zbi_header_t{.type = ZBI_TYPE_KERNEL_DRIVER, .extra = extra}, bytes);
    if (result.is_error()) {
      printf("mexec: failed to append UART data to data ZBI: ");
      zbitl::PrintViewError(result.error_value());
      return error(result.error_value());
    }
    return ZX_OK;
  };
  if (auto pio_uart = ktl::get_if<dcfg_simple_pio_t>(&bootloader.uart)) {
    if (zx_status_t status =
            add_uart(KDRV_I8250_PIO_UART, zbitl::AsBytes(pio_uart, sizeof(*pio_uart)));
        status != ZX_OK) {
      return status;
    }
  } else if (auto mmio_uart = ktl::get_if<dcfg_simple_t>(&bootloader.uart)) {
    if (zx_status_t status =
            add_uart(KDRV_I8250_MMIO_UART, zbitl::AsBytes(mmio_uart, sizeof(*mmio_uart)));
        status != ZX_OK) {
      return status;
    }
  } else {
    ZX_DEBUG_ASSERT_MSG(ktl::get_if<ktl::monostate>(&bootloader.uart),
                        "bootloader.uart in impossible ktl::variant state???");
  }

  if (bootloader.nvram.base) {
    auto result =
        image.Append(zbi_header_t{.type = ZBI_TYPE_NVRAM}, zbitl::AsBytes(bootloader.nvram));
    if (result.is_error()) {
      printf("mexec: failed to append NVRAM data to data ZBI: ");
      zbitl::PrintViewError(result.error_value());
      return error(result.error_value());
    }
  }

  return ZX_OK;
}

// Number of pages required to identity map 8GiB of memory.
constexpr size_t kBytesToIdentityMap = 8ull * GB;
constexpr size_t kNumL2PageTables = kBytesToIdentityMap / (2ull * MB * NO_OF_PT_ENTRIES);
constexpr size_t kNumL3PageTables = 1;
constexpr size_t kNumL4PageTables = 1;
constexpr size_t kTotalPageTableCount = kNumL2PageTables + kNumL3PageTables + kNumL4PageTables;

static fbl::RefPtr<VmAspace> mexec_identity_aspace;

// Array of pages that are safe to use for the new kernel's page tables.  These must
// be after where the new boot image will be placed during mexec.  This array is
// populated in platform_mexec_prep and used in platform_mexec.
static paddr_t mexec_safe_pages[kTotalPageTableCount];

void platform_mexec_prep(uintptr_t final_bootimage_addr, size_t final_bootimage_len) {
  DEBUG_ASSERT(!arch_ints_disabled());
  DEBUG_ASSERT(mp_get_online_mask() == cpu_num_to_mask(BOOT_CPU_ID));

  // A hacky way to handle disabling all PCI devices until we have devhost
  // lifecycles implemented.
  // Leaving PCI running will also leave DMA running which may cause memory
  // corruption after boot.
  // Disabling PCI may cause devices to fail to enumerate after boot.
#ifdef WITH_KERNEL_PCIE
  if (gBootOptions->mexec_pci_shutdown) {
    PcieBusDriver::GetDriver()->DisableBus();
  }
#endif

  // This code only handles one L3 and one L4 page table for now. Fail if
  // there are more L2 page tables than can fit in one L3 page table.
  static_assert(kNumL2PageTables <= NO_OF_PT_ENTRIES,
                "Kexec identity map size is too large. Only one L3 PTE is supported at this time.");
  static_assert(kNumL3PageTables == 1, "Only 1 L3 page table is supported at this time.");
  static_assert(kNumL4PageTables == 1, "Only 1 L4 page table is supported at this time.");

  // Identity map the first 8GiB of RAM
  mexec_identity_aspace = VmAspace::Create(VmAspace::TYPE_LOW_KERNEL, "x86-64 mexec 1:1");
  DEBUG_ASSERT(mexec_identity_aspace);

  const uint perm_flags_rwx =
      ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE | ARCH_MMU_FLAG_PERM_EXECUTE;
  void* identity_address = 0x0;
  paddr_t pa = 0;
  zx_status_t result =
      mexec_identity_aspace->AllocPhysical("1:1 mapping", kBytesToIdentityMap, &identity_address, 0,
                                           pa, VmAspace::VMM_FLAG_VALLOC_SPECIFIC, perm_flags_rwx);
  if (result != ZX_OK) {
    panic("failed to identity map low memory");
  }

  alloc_pages_greater_than(final_bootimage_addr + final_bootimage_len + PAGE_SIZE,
                           kTotalPageTableCount, kBytesToIdentityMap, mexec_safe_pages);
}

void platform_mexec(mexec_asm_func mexec_assembly, memmov_ops_t* ops, uintptr_t new_bootimage_addr,
                    size_t new_bootimage_len, uintptr_t entry64_addr) {
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(mp_get_online_mask() == cpu_num_to_mask(BOOT_CPU_ID));

  // This code only handles one L3 and one L4 page table for now. Fail if
  // there are more L2 page tables than can fit in one L3 page table.
  static_assert(kNumL2PageTables <= NO_OF_PT_ENTRIES,
                "Kexec identity map size is too large. Only one L3 PTE is supported at this time.");
  static_assert(kNumL3PageTables == 1, "Only 1 L3 page table is supported at this time.");
  static_assert(kNumL4PageTables == 1, "Only 1 L4 page table is supported at this time.");
  DEBUG_ASSERT(mexec_identity_aspace);

  vmm_set_active_aspace(mexec_identity_aspace.get());

  size_t safe_page_id = 0;
  volatile pt_entry_t* ptl4 = (pt_entry_t*)paddr_to_physmap(mexec_safe_pages[safe_page_id++]);
  volatile pt_entry_t* ptl3 = (pt_entry_t*)paddr_to_physmap(mexec_safe_pages[safe_page_id++]);

  // Initialize these to 0
  for (size_t i = 0; i < NO_OF_PT_ENTRIES; i++) {
    ptl4[i] = 0;
    ptl3[i] = 0;
  }

  for (size_t i = 0; i < kNumL2PageTables; i++) {
    ptl3[i] = mexec_safe_pages[safe_page_id] | X86_KERNEL_PD_FLAGS;
    volatile pt_entry_t* ptl2 = (pt_entry_t*)paddr_to_physmap(mexec_safe_pages[safe_page_id]);

    for (size_t j = 0; j < NO_OF_PT_ENTRIES; j++) {
      ptl2[j] = (2 * MB * (i * NO_OF_PT_ENTRIES + j)) | X86_KERNEL_PD_LP_FLAGS;
    }

    safe_page_id++;
  }

  ptl4[0] = vaddr_to_paddr((void*)ptl3) | X86_KERNEL_PD_FLAGS;

  mexec_assembly((uintptr_t)new_bootimage_addr, vaddr_to_paddr((void*)ptl4), entry64_addr, 0, ops,
                 0);
}

void platform_early_init(void) {
  /* extract bootloader data while still accessible */
  /* this includes debug uart config, etc. */
  platform_save_bootloader_data();
  FinishBootOptions();

  /* is the cmdline option to bypass dlog set ? */
  dlog_bypass_init();

  /* get the debug output working */
  pc_init_debug_early();

#if WITH_LEGACY_PC_CONSOLE
  /* get the text console working */
  platform_init_console();
#endif

  /* if the bootloader has framebuffer info, use it for early console */
  platform_early_display_init();

  /* initialize the ACPI parser */
  PlatformInitAcpi(bootloader.acpi_rsdp);

  /* initialize the boot memory reservation system */
  boot_reserve_init();

  // Add the data ZBI to the boot reserve list.
  boot_reserve_zbi();

  /* initialize physical memory arenas */
  pc_mem_init(bootloader.memory_ranges);

  /* wire all of the reserved boot sections */
  boot_reserve_wire();
}

void platform_prevm_init() {}

// Maps from contiguous id to APICID.
static fbl::Vector<uint32_t> apic_ids;
static size_t bsp_apic_id_index;

static void traverse_topology(uint32_t) {
  // Filter out hyperthreads if we've been told not to init them
  const bool use_ht = gBootOptions->smp_ht_enabled;

  // We're implicitly running on the BSP
  const uint32_t bsp_apic_id = apic_local_id();
  DEBUG_ASSERT(bsp_apic_id == apic_bsp_id());

  // Maps from contiguous id to logical id in topology.
  fbl::Vector<cpu_num_t> logical_ids;

  // Iterate over all the cores, copy apic ids of active cores into list.
  dprintf(INFO, "cpu topology:\n");
  size_t cpu_index = 0;
  bsp_apic_id_index = 0;
  for (const auto* processor_node : system_topology::GetSystemTopology().processors()) {
    const auto& processor = processor_node->entity.processor;
    for (size_t i = 0; i < processor.architecture_info.x86.apic_id_count; i++) {
      const uint32_t apic_id = processor.architecture_info.x86.apic_ids[i];
      const bool keep = (i < 1) || use_ht;
      const size_t index = cpu_index++;

      dprintf(INFO, "\t%3zu: apic id %#4x %s%s%s\n", index, apic_id, (i > 0) ? "SMT " : "",
              (apic_id == bsp_apic_id) ? "BSP " : "", keep ? "" : "(not using)");

      if (keep) {
        if (apic_id == bsp_apic_id) {
          bsp_apic_id_index = apic_ids.size();
        }

        fbl::AllocChecker ac;
        apic_ids.push_back(apic_id, &ac);
        if (!ac.check()) {
          dprintf(CRITICAL, "Failed to allocate apic_ids table, disabling SMP!\n");
          return;
        }
        logical_ids.push_back(static_cast<cpu_num_t>(index), &ac);
        if (!ac.check()) {
          dprintf(CRITICAL, "Failed to allocate logical_ids table, disabling SMP!\n");
          return;
        }
      }
    }
  }

  // Find the CPU count limit
  uint32_t max_cpus = gBootOptions->smp_max_cpus;
  if (max_cpus > SMP_MAX_CPUS || max_cpus <= 0) {
    printf("invalid kernel.smp.maxcpus value, defaulting to %d\n", SMP_MAX_CPUS);
    max_cpus = SMP_MAX_CPUS;
  }

  dprintf(INFO, "Found %zu cpu%c\n", apic_ids.size(), (apic_ids.size() > 1) ? 's' : ' ');
  if (apic_ids.size() > max_cpus) {
    dprintf(INFO, "Clamping number of CPUs to %u\n", max_cpus);
    // TODO(edcoyne): Implement fbl::Vector()::resize().
    while (apic_ids.size() > max_cpus) {
      apic_ids.pop_back();
      logical_ids.pop_back();
    }
  }

  if (apic_ids.size() == max_cpus || !use_ht) {
    // If we are at the max number of CPUs, or have filtered out
    // hyperthreads, sanity check that the bootstrap processor is in the set.
    bool found_bp = false;
    for (const auto apic_id : apic_ids) {
      if (apic_id == bsp_apic_id) {
        found_bp = true;
        break;
      }
    }
    ASSERT(found_bp);
  }

  const size_t cpu_count = logical_ids.size();
  CpuDistanceMap::Initialize(cpu_count, [&logical_ids](cpu_num_t from_id, cpu_num_t to_id) {
    using system_topology::Node;
    using system_topology::Graph;

    const cpu_num_t logical_from_id = logical_ids[from_id];
    const cpu_num_t logical_to_id = logical_ids[to_id];
    const Graph& topology = system_topology::GetSystemTopology();

    Node* from_node = nullptr;
    if (topology.ProcessorByLogicalId(logical_from_id, &from_node) != ZX_OK) {
      printf("Failed to get processor node for logical CPU %u\n", logical_from_id);
      return -1;
    }
    DEBUG_ASSERT(from_node != nullptr);

    Node* to_node = nullptr;
    if (topology.ProcessorByLogicalId(logical_to_id, &to_node) != ZX_OK) {
      printf("Failed to get processor node for logical CPU %u\n", logical_to_id);
      return -1;
    }
    DEBUG_ASSERT(to_node != nullptr);

    Node* from_cache_node = nullptr;
    for (Node* node = from_node->parent; node != nullptr; node = node->parent) {
      if (node->entity_type == ZBI_TOPOLOGY_ENTITY_CACHE) {
        from_cache_node = node;
        break;
      }
    }
    Node* to_cache_node = nullptr;
    for (Node* node = to_node->parent; node != nullptr; node = node->parent) {
      if (node->entity_type == ZBI_TOPOLOGY_ENTITY_CACHE) {
        to_cache_node = node;
        break;
      }
    }

    const uint32_t from_cache_id = from_cache_node ? from_cache_node->entity.cache.cache_id : 0;
    const uint32_t to_cache_id = to_cache_node ? to_cache_node->entity.cache.cache_id : 0;

    // Return the maximum cache depth that is not shared by the CPUs.
    // TODO(eieio): Consider NUMA node and other caches.
    return ktl::max(
        {1 * int{logical_from_id != logical_to_id}, 2 * int{from_cache_id != to_cache_id}});
  });

  // TODO(eieio): Determine this automatically. The current value matches the
  // distance value of the cache above.
  const CpuDistanceMap::Distance kDistanceThreshold = 2u;
  CpuDistanceMap::Get().set_distance_threshold(kDistanceThreshold);

  CpuDistanceMap::Get().Dump();
}
LK_INIT_HOOK(pc_traverse_topology, traverse_topology, LK_INIT_LEVEL_TOPOLOGY)

// Must be called after traverse_topology has processed the SMP data.
static void platform_init_smp() {
  x86_init_smp(apic_ids.data(), static_cast<uint32_t>(apic_ids.size()));

  // trim the boot cpu out of the apic id list before passing to the AP booting routine
  apic_ids.erase(bsp_apic_id_index);

  x86_bringup_aps(apic_ids.data(), static_cast<uint32_t>(apic_ids.size()));
}

zx_status_t platform_mp_prep_cpu_unplug(cpu_num_t cpu_id) {
  // TODO: Make sure the IOAPIC and PCI have nothing for this CPU
  return arch_mp_prep_cpu_unplug(cpu_id);
}

zx_status_t platform_mp_cpu_unplug(cpu_num_t cpu_id) { return arch_mp_cpu_unplug(cpu_id); }

const char* manufacturer = "unknown";
const char* product = "unknown";

void platform_init(void) {
  pc_init_debug();

  platform_init_crashlog();

#if NO_USER_KEYBOARD
  platform_init_keyboard(&console_input_buf);
#endif

  // Initialize all PvEoi instances prior to starting secondary CPUs.
  PvEoi::InitAll();

  platform_init_smp();

  pc_init_smbios();

  SmbiosWalkStructs([](smbios::SpecVersion version, const smbios::Header* h,
                       const smbios::StringTable& st) -> zx_status_t {
    if (h->type == smbios::StructType::SystemInfo && version.IncludesVersion(2, 0)) {
      auto entry = reinterpret_cast<const smbios::SystemInformationStruct2_0*>(h);
      st.GetString(entry->manufacturer_str_idx, &manufacturer);
      st.GetString(entry->product_name_str_idx, &product);
    }
    return ZX_OK;
  });
  printf("smbios: manufacturer=\"%s\" product=\"%s\"\n", manufacturer, product);
}

void platform_suspend(void) {
  pc_prep_suspend_timer();
  pc_suspend_debug();
}

void platform_resume(void) {
  pc_resume_debug();
  pc_resume_timer();
}
