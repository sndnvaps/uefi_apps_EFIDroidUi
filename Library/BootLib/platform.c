/*
 * Copyright 2016, The EFIDroid Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/

#include <lib/boot.h>
#include <lib/boot/internal/boot_internal.h>

#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Library/Util.h>
#include <LittleKernel.h>

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

extern lkapi_t *mLKApi;

boot_uint32_t libboot_qcdt_pmic_target(boot_uint8_t num_ent) {
    if(mLKApi)
        return mLKApi->boot_get_pmic_target(num_ent);
    return 0;
}

boot_uint32_t libboot_qcdt_platform_id(void) {
    if(mLKApi)
        return mLKApi->boot_get_platform_id();
    return 0;
}

boot_uint32_t libboot_qcdt_hardware_id(void) {
    if(mLKApi)
        return mLKApi->boot_get_hardware_id();
    return 0;
}

boot_uint32_t libboot_qcdt_hardware_subtype(void) {
    if(mLKApi)
        return mLKApi->boot_get_hardware_subtype();
    return 0;
}

boot_uint32_t libboot_qcdt_soc_version(void) {
    if(mLKApi)
        return mLKApi->boot_get_soc_version();
    return 0;
}

boot_uint32_t libboot_qcdt_target_id(void) {
    if(mLKApi)
        return mLKApi->boot_get_target_id();
    return 0;
}

boot_uint32_t libboot_qcdt_foundry_id(void) {
    if(mLKApi)
        return mLKApi->boot_get_foundry_id();
    return 0;
}

boot_uint32_t libboot_qcdt_get_hlos_subtype(void) {
    if(mLKApi)
        return mLKApi->boot_get_hlos_subtype();
    return 0;
}

void libboot_platform_memmove(void* dst, const void* src, boot_uintn_t num) {
    CopyMem(dst, src, num);
}

int libboot_platform_memcmp(const void *s1, const void *s2, boot_uintn_t n) {
    return (int)CompareMem(s1, s2, n);
}

void *libboot_platform_memset(void *s, int c, boot_uintn_t n) {
    return SetMem(s, (UINTN)n, (UINT8)c);
}

void* libboot_platform_alloc(boot_uintn_t size) {
    void* mem = AllocatePool(size);
    if(!mem)
        libboot_format_error(LIBBOOT_ERROR_GROUP_COMMON, LIBBOOT_ERROR_COMMON_OUT_OF_MEMORY);
    return mem;
}

void libboot_platform_free(void *ptr) {
    if(ptr)
        FreePool(ptr);
}

void libboot_platform_format_string(char* buf, boot_uintn_t sz, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sz, fmt, args);
    va_end (args);
}

char* libboot_platform_strdup(const char *s) {
    return AllocateCopyPool(AsciiStrSize (s), s);
}

char* libboot_platform_strtok_r(char *str, const char *delim, char **saveptr) {
    return strtok_r(str, delim, saveptr);
}

char* libboot_platform_strchr(const char *s, int c) {
    return strchr(s, c);
}

int libboot_platform_strcmp(const char* str1, const char* str2) {
    return (int)AsciiStrCmp(str1, str2);
}

boot_uintn_t libboot_platform_strlen(const char* str) {
    return AsciiStrLen(str);
}

void* libboot_platform_getmemory(void *pdata, libboot_platform_getmemory_callback_t cb) {
  LIST_ENTRY                  *ResourceLink;
  LIST_ENTRY                  ResourceList;
  SYSTEM_MEMORY_RESOURCE      *Resource;

  UtilGetSystemMemoryResources (&ResourceList);
  ResourceLink = ResourceList.ForwardLink;
  while (ResourceLink != NULL && ResourceLink != &ResourceList) {
    Resource = (SYSTEM_MEMORY_RESOURCE*)ResourceLink;
    DEBUG ((EFI_D_INFO, "- [0x%08X,0x%08X]\n",
        (UINT32)Resource->PhysicalStart,
        (UINT32)Resource->PhysicalStart + (UINT32)Resource->ResourceLength));
    pdata = cb(pdata, Resource->PhysicalStart, Resource->ResourceLength);
    ResourceLink = ResourceLink->ForwardLink;
  }

  return pdata;
}

boot_uintn_t libboot_platform_machtype(void) {
    if(mLKApi)
        return mLKApi->boot_get_machine_type();
    return 0;
}

void* libboot_platform_bigalloc(boot_uintn_t size) {
    return AllocatePool(size);
}

void libboot_platform_bigfree(void *ptr) {
  if(ptr)
    FreePool(ptr);
}

void* libboot_platform_bootalloc(boot_uintn_t addr, boot_uintn_t sz) {
  printf("alloc: 0x%08x - 0x%08x ; 0x%08x\n", addr, addr+sz, sz);
  UINTN      AlignedSize = sz;
  UINTN      AddrOffset = 0;
  EFI_PHYSICAL_ADDRESS AllocationAddress = AlignMemoryRange(addr, &AlignedSize, &AddrOffset, EFI_PAGE_SIZE);

  EFI_STATUS Status =  gBS->AllocatePages (AllocateAddress, EfiBootServicesData, EFI_SIZE_TO_PAGES(AlignedSize), &AllocationAddress);
  if(EFI_ERROR(Status))
    return NULL;

  return (VOID*)((UINTN)AllocationAddress)+AddrOffset;
}

void libboot_platform_bootfree(boot_uintn_t addr, boot_uintn_t sz) {
    FreeAlignedMemoryRange(addr, sz, EFI_PAGE_SIZE);
}
