/* $Id$ */
/** @file
 * Wrappers for a number of common Windows APIs.
 */

/*
 * Copyright (c) 2008 knut st. osmundsen <bird-src-spam@anduin.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with This program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define _ADVAPI32_
#define _KERNEL32_
#define _WIN32_WINNT 0x0600
#define UNICODE
#include <Windows.h>
#include <TLHelp32.h>
#include <k/kDefs.h>

#if K_ARCH == K_ARCH_X86_32
typedef PVOID PRUNTIME_FUNCTION;
typedef FARPROC PGET_RUNTIME_FUNCTION_CALLBACK;
#endif

/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
typedef struct KPRF2WRAPDLL
{
    HMODULE hmod;
    char szName[32];
} KPRF2WRAPDLL;
typedef KPRF2WRAPDLL *PKPRF2WRAPDLL;
typedef KPRF2WRAPDLL const *PCKPRF2WRAPDLL;


/* TODO (amd64):

AddLocalAlternateComputerNameA
AddLocalAlternateComputerNameW
EnumerateLocalComputerNamesA
EnumerateLocalComputerNamesW
RemoveLocalAlternateComputerNameA
RemoveLocalAlternateComputerNameW

RtlLookupFunctionEntry
RtlPcToFileHeader
RtlRaiseException
RtlVirtualUnwind

SetConsoleCursor
SetLocalPrimaryComputerNameA
SetLocalPrimaryComputerNameW
__C_specific_handler
__misaligned_access
_local_unwind
*/

/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
KPRF2WRAPDLL g_Kernel32 =
{
    INVALID_HANDLE_VALUE, "KERNEL32"
};


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
FARPROC kPrf2WrapResolve(void **ppfn, const char *pszName, PKPRF2WRAPDLL pDll);


FARPROC kPrf2WrapResolve(void **ppfn, const char *pszName, PKPRF2WRAPDLL pDll)
{
    FARPROC pfn;
    HMODULE hmod = pDll->hmod;
    if (hmod == INVALID_HANDLE_VALUE)
    {
        hmod = LoadLibraryA(pDll->szName);
        pDll->hmod = hmod;
    }

    pfn = GetProcAddress(hmod, pszName);
    *ppfn = (void *)pfn;
    return pfn;
}


#include "kPrf2WinApiWrappers-kernel32.h"
