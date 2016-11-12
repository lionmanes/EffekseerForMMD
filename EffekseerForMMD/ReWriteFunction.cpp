﻿#include <stdio.h>
#include <windows.h>
#include <imagehlp.h>
#include <tlhelp32.h>
#pragma comment(lib, "Dbghelp")
#pragma comment(lib,"imagehlp.lib")
LPVOID RvaToVa(LPVOID pvBase, DWORD dwRva)
{
  return static_cast<LPBYTE>(pvBase) + dwRva;
}

LPVOID RvaToVa(LPVOID pvBase, DWORD dwRva, BOOL bLoaded, IMAGE_SECTION_HEADER** ppSectionHeader = nullptr)
{
  auto va = static_cast<LPBYTE>(RvaToVa(pvBase, dwRva));

  if ( !bLoaded )
  {
    auto pNtHeaders = ImageNtHeader(pvBase);
    auto pSectionHeader = IMAGE_FIRST_SECTION(pNtHeaders);

    for ( int i = 0; i < pNtHeaders->FileHeader.NumberOfSections; ++i , ++pSectionHeader )
    {
      auto pSection = static_cast<LPBYTE>(RvaToVa(pvBase, pSectionHeader->VirtualAddress));
      if ( pSection <= va && va < (pSection + pSectionHeader->SizeOfRawData) )
      {
        auto diff = pSectionHeader->VirtualAddress - pSectionHeader->PointerToRawData;
        va -= diff;

        if ( ppSectionHeader != nullptr )
        {
          *ppSectionHeader = pSectionHeader;
        }

        break;
      }
    }
  }

  return va;
}

void* RewriteFunction(const char* szRewriteModuleName, const char* szRewriteFunctionName, void* pRewriteFunctionPointer, int ordinal)
{
  auto dwBase = static_cast<HMODULE>(::GetModuleHandleA(szRewriteModuleName));

  if ( !dwBase )
  {
    return nullptr;
  }

  ULONG ulSize = 0;
  PIMAGE_IMPORT_DESCRIPTOR pImgDesc = static_cast<PIMAGE_IMPORT_DESCRIPTOR>(ImageDirectoryEntryToData(dwBase, TRUE, IMAGE_DIRECTORY_ENTRY_IMPORT, &ulSize));
  for ( ; pImgDesc->Characteristics != 0; pImgDesc++ )
  {
    // THUNK情報
    auto pFirstThunk = static_cast<PIMAGE_THUNK_DATA>(RvaToVa(dwBase, pImgDesc->FirstThunk, TRUE));
    auto pOrgFirstThunk = static_cast<PIMAGE_THUNK_DATA>(RvaToVa(dwBase, pImgDesc->OriginalFirstThunk, TRUE));
    // 関数列挙
    for ( ; pFirstThunk && pOrgFirstThunk && pFirstThunk->u1.Function; pFirstThunk++ , pOrgFirstThunk++ )
    {
      bool ok = false;
      if ( IMAGE_SNAP_BY_ORDINAL(pOrgFirstThunk->u1.Ordinal) )
      {
        int tmp = IMAGE_ORDINAL(pOrgFirstThunk->u1.Ordinal);
        ok = tmp == ordinal;
      }
      auto pImportName = static_cast<PIMAGE_IMPORT_BY_NAME>(RvaToVa(dwBase, pOrgFirstThunk->u1.AddressOfData, TRUE));

      if ( !szRewriteFunctionName )
      {
        continue;
      }

      if ( !ok && strcmp(static_cast<const char*>(pImportName->Name), szRewriteFunctionName) != 0 ) continue;

      DWORD dwOldProtect;
      if ( !VirtualProtect(&pFirstThunk->u1.Function, sizeof(pFirstThunk->u1.Function), PAGE_READWRITE, &dwOldProtect) )
      {
        puts("VirtualProtect error");
        return nullptr; // エラー
      }

      auto pOrgFunc = reinterpret_cast<void*>(static_cast<intptr_t>(pFirstThunk->u1.Function));

      if ( WriteProcessMemory(GetCurrentProcess(), &pFirstThunk->u1.Function, &pRewriteFunctionPointer, sizeof(pFirstThunk->u1.Function), nullptr) ) { }
      else
      {
        puts("WriteProcessMemory error");
      }

      VirtualProtect(&pFirstThunk->u1.Function, sizeof(pFirstThunk->u1.Function), dwOldProtect, &dwOldProtect);
      return pOrgFunc;
    }
  }
  printf("not found : module = %s ,funcname = %s\n", szRewriteModuleName, szRewriteFunctionName);
  return nullptr;
}

void PrintFunctions()
{
  printf("----\n");
  RewriteFunction(nullptr, nullptr, nullptr, -1);
  printf("----\n");
}


void modifyIATonemod(char* modname, void* origaddr, void* newaddr, HMODULE hModule)
{
  ULONG ulSize;
  auto pImportDesc = static_cast<PIMAGE_IMPORT_DESCRIPTOR>(ImageDirectoryEntryToData(hModule, TRUE, IMAGE_DIRECTORY_ENTRY_IMPORT, &ulSize));
  if ( pImportDesc == nullptr )
  {
    return;
  }

  /* seek the target DLL */
  while ( pImportDesc->Name )
  {
    char* name = reinterpret_cast<char*>(hModule) + pImportDesc->Name;
    if ( lstrcmpiA(name, modname) == 0 )
    {
      break;
    }
    pImportDesc++;
  }
  if ( pImportDesc->Name == 0 )
  {
    return;
  }

  /* modify corrensponding IAT entry */
  auto pThunk = reinterpret_cast<PIMAGE_THUNK_DATA>(reinterpret_cast<char *>(hModule) + pImportDesc->FirstThunk);
  while ( pThunk->u1.Function )
  {
    auto paddr = reinterpret_cast<PROC*>(&pThunk->u1.Function);
    if ( *paddr == origaddr )
    {
      DWORD flOldProtect;
      VirtualProtect(paddr, sizeof(paddr), PAGE_EXECUTE_READWRITE, &flOldProtect);
      *paddr = static_cast<PROC>(newaddr);
      VirtualProtect(paddr, sizeof(paddr), flOldProtect, &flOldProtect);
    }
    pThunk++;
  }
}

void modifyIAT(char* modname, void* origaddr, void* newaddr)
{
  HANDLE hModuleSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());

  MODULEENTRY32 me;
  me.dwSize = sizeof(me);

  /* modify IAT in all loaded modules */
  BOOL bModuleResult = Module32First(hModuleSnap, &me);
  while ( bModuleResult )
  {
    modifyIATonemod(modname, origaddr, newaddr, me.hModule);
    bModuleResult = Module32Next(hModuleSnap, &me);
  }

  CloseHandle(hModuleSnap);
}
