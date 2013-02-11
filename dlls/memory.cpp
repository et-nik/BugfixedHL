/***
*
*	Copyright (c) 2012, AGHL.RU. All rights reserved.
*
****/
//
// Memory.cpp
//
// Runtime memory searching/patching
//

#include <windows.h>
#include <psapi.h>

#include "memory.h"
#include "wrect.h"
#include "cl_dll.h"
#include "cvardef.h"
#include "jpge.h"
#include "cl_util.h"
#include "results.h"
#include "parsemsg.h"

#define MAX_PATTERN 128


typedef void (__fastcall *ThisCallInt)(void *, int, int);
typedef void (__fastcall *ThisCallIntInt)(void *, int, int, int);


bool g_bPatchStatusPrinted = false;
char g_szPatchErrors[2048];

/* Engine addresses */
size_t g_EngineModuleBase = 0, g_EngineModuleSize = 0, g_EngineModuleEnd = 0;

/* GameUI addresses */
size_t g_GameUiModuleBase = 0, g_GameUiModuleSize = 0, g_GameUiModuleEnd = 0;

/* Messages variables */
size_t g_SvcMessagesTable = 0;
void **g_EngineBuf = 0;
int *g_EngineBufSize = 0;
int *g_EngineReadPos = 0;
UserMessage **g_pUserMessages = 0;

/* FPS bugfix variables */
cvar_t *m_pCvarEngineFixFpsBug = 0;
size_t g_FpsBugPlace = 0;
uint8_t g_FpsBugPlaceBackup[16];
double *g_flFrameTime = 0;
double g_flFrameTimeReminder = 0;

/* Snapshot variables */
cvar_t *m_pCvarEngineSnapshotHook = 0, *m_pCvarSnapshotJpeg = 0, *m_pCvarSnapshotJpegQuality = 0;
int (*g_pEngineSnapshotCommandHandler)(void) = 0;
int (*g_pEngineCreateSnapshot)(char *filename) = 0;
int *g_piScreenWidth = 0, *g_piScreenHeight = 0;
int (__stdcall **g_pGlReadPixels)(int x, int y, int width, int height, DWORD format, DWORD type, void *data) = 0;
#define GL_RGB				0x1907
#define GL_UNSIGNED_BYTE	0x1401

/* Commands variables */
CommandLink **g_pCommandsList = 0;
void (*g_pOldMotdWriteHandler)(void) = 0;

/* Connectionless packets */
void (*g_pEngineClConnectionlessPacketHandler)(void) = 0;
size_t g_pEngineClConnectionlessPacketPlace = 0;
uint32_t g_pEngineClConnectionlessPacketOffset = 0;

/* GameUI fix variables */
int g_iLinesCounter = 0;
ThisCallInt g_pFunctionReplacedByCounter;
ThisCallIntInt g_pFunctionReplacedBySubst;
size_t (*g_pGet_VGUI_System009)(void);
size_t *g_pSystem = 0;

bool GetModuleAddress(const char *moduleName, size_t &moduleBase, size_t &moduleSize)
{
	HANDLE hProcess = GetCurrentProcess();
	HMODULE hModuleDll = GetModuleHandle(moduleName);
	if (!hProcess || !hModuleDll) return false;
	MODULEINFO moduleInfo;
	GetModuleInformation(hProcess, hModuleDll, &moduleInfo, sizeof(moduleInfo));
	moduleBase = (size_t)moduleInfo.lpBaseOfDll;
	moduleSize = (size_t)moduleInfo.SizeOfImage;
	return true;
}
// Searches for engine address in memory
void GetEngineModuleAddress(void)
{
	if (!GetModuleAddress("hw.dll", g_EngineModuleBase, g_EngineModuleSize) &&	// Try Hardware engine
		!GetModuleAddress("sw.dll", g_EngineModuleBase, g_EngineModuleSize) &&	// Try Software engine
		!GetModuleAddress("hl.exe", g_EngineModuleBase, g_EngineModuleSize))	// Try Encrypted engine
		return;
	g_EngineModuleEnd = g_EngineModuleBase + g_EngineModuleSize - 1;
}
void GetGameUiModuleAddress(void)
{
	if (!GetModuleAddress("GameUI.dll", g_GameUiModuleBase, g_GameUiModuleSize))
		return;
	g_GameUiModuleEnd = g_GameUiModuleBase + g_GameUiModuleSize - 1;
}

// Converts HEX string containing pairs of symbols 0-9, A-F, a-f with possible space splitting into byte array
size_t ConvertHexString(const char *srcHexString, unsigned char *outBuffer, size_t bufferSize)
{
	unsigned char *in = (unsigned char *)srcHexString;
	unsigned char *out = outBuffer;
	unsigned char *end = outBuffer + bufferSize;
	bool low = false;
	uint8_t byte = 0;
	while (*in && out < end)
	{
		if (*in >= '0' && *in <= '9') { byte |= *in - '0'; }
		else if (*in >= 'A' && *in <= 'F') { byte |= *in - 'A' + 10; }
		else if (*in >= 'a' && *in <= 'f') { byte |= *in - 'a' + 10; }
		else if (*in == ' ') { in++; continue; }

		if (!low)
		{
			byte = byte << 4;
			in++;
			low = true;
			continue;
		}
		low = false;

		*out = byte;
		byte = 0;

		in++;
		out++;
	}
	return out - outBuffer;
}
size_t MemoryFindForward(size_t start, size_t end, const unsigned char *pattern, const unsigned char *mask, size_t pattern_len)
{
	// Ensure start is lower then the end
	if (start > end)
	{
		size_t reverse = end;
		end = start;
		start = reverse;
	}

	unsigned char *cend = (unsigned char*)(end - pattern_len + 1);
	unsigned char *current = (unsigned char*)(start);

	// Just linear search for sequence of bytes from the start till the end minus pattern length
	size_t i;
	if (mask)
	{
		// honoring mask
		while (current < cend)
		{
			for (i = 0; i < pattern_len; i++)
			{
				if ((current[i] & mask[i]) != (pattern[i] & mask[i]))
					break;
			}

			if (i == pattern_len)
				return (size_t)(void*)current;

			current++;
		}
	}
	else
	{
		// without mask
		while (current < cend)
		{
			for (i = 0; i < pattern_len; i++)
			{
				if (current[i] != pattern[i])
					break;
			}

			if (i == pattern_len)
				return (size_t)(void*)current;

			current++;
		}
	}

	return NULL;
}
// Signed char versions assume pattern and mask are in HEX string format and perform conversions
size_t MemoryFindForward(size_t start, size_t end, const char *pattern, const char *mask)
{
	unsigned char p[MAX_PATTERN];
	unsigned char m[MAX_PATTERN];
	size_t pl = ConvertHexString(pattern, p, sizeof(p));
	size_t ml = ConvertHexString(mask, m, sizeof(m));
	return MemoryFindForward(start, end, p, m, pl >= ml ? pl : ml);
}
size_t MemoryFindBackward(size_t start, size_t end, const unsigned char *pattern, const unsigned char *mask, size_t pattern_len)
{
	// Ensure start is higher then the end
	if (start < end)
	{
		size_t reverse = end;
		end = start;
		start = reverse;
	}

	unsigned char *cend = (unsigned char*)(end);
	unsigned char *current = (unsigned char*)(start - pattern_len);

	// Just linear search backward for sequence of bytes from the start minus pattern length till the end
	size_t i;
	if (mask)
	{
		// honoring mask
		while (current >= cend)
		{
			for (i = 0; i < pattern_len; i++)
			{
				if ((current[i] & mask[i]) != (pattern[i] & mask[i]))
					break;
			}

			if (i == pattern_len)
				return (size_t)(void*)current;

			current--;
		}
	}
	else
	{
		// without mask
		while (current >= cend)
		{
			for (i = 0; i < pattern_len; i++)
			{
				if (current[i] != pattern[i])
					break;
			}

			if (i == pattern_len)
				return (size_t)(void*)current;

			current--;
		}
	}

	return NULL;
}
// Signed char versions assume pattern and mask are in HEX string format and perform conversions
size_t MemoryFindBackward(size_t start, size_t end, const char *pattern, const char *mask)
{
	unsigned char p[MAX_PATTERN];
	unsigned char m[MAX_PATTERN];
	size_t pl = ConvertHexString(pattern, p, sizeof(p));
	size_t ml = ConvertHexString(mask, m, sizeof(m));
	return MemoryFindBackward(start, end, p, m, pl >= ml ? pl : ml);
}

// Replaces double word on specified address with new dword, returns old dword
uint32_t HookDWord(size_t origAddr, uint32_t newDWord)
{
	DWORD oldProtect;
	uint32_t origDWord = *(size_t *)origAddr;
	VirtualProtect((size_t *)origAddr, 4, PAGE_EXECUTE_READWRITE, &oldProtect);
	*(size_t *)origAddr = newDWord;
	VirtualProtect((size_t *)origAddr, 4, oldProtect, &oldProtect);
	return origDWord;
}
// Exchanges bytes between memory address and bytes array
void ExchangeMemoryBytes(size_t *origAddr, size_t *dataAddr, uint32_t size)
{
	DWORD oldProtect;
	VirtualProtect(origAddr, size, PAGE_EXECUTE_READWRITE, &oldProtect);
	unsigned char data[MAX_PATTERN];
	int32_t iSize = size;
	while (iSize > 0)
	{
		size_t s = iSize <= MAX_PATTERN ? iSize : MAX_PATTERN;
		memcpy(data, origAddr, s);
		memcpy((void *)origAddr, (void *)dataAddr, s);
		memcpy((void *)dataAddr, data, s);
		iSize -= MAX_PATTERN;
	}
	VirtualProtect(origAddr, size, oldProtect, &oldProtect);
}

// Function that eliminates FPS bug, it is using stdcall convention so it will clear the stack
void __stdcall FpsBugFix(int a1, int64_t *a2)
{
	if (!m_pCvarEngineFixFpsBug || m_pCvarEngineFixFpsBug->value)
	{
		// Collect the reminder and use it when it is over 1
		g_flFrameTimeReminder += *g_flFrameTime * 1000 - a1;
		if (g_flFrameTimeReminder > 1.0)
		{
			g_flFrameTimeReminder--;
			a1++;
		}
	}
	// Place fixed value on a stack and do actions that our patch had overwritten in original function
	*a2 = a1;
	*((double *)(a2 + 1)) = a1;
}

// Our snapshot command handler that perform saving to jpeg
void SnapshotCmdHandler(void)
{
	if (m_pCvarEngineSnapshotHook && m_pCvarEngineSnapshotHook->value)
	{
		char filename[MAX_PATH];
		char fullpath[MAX_PATH];
		// Do snapshot
		if (m_pCvarSnapshotJpeg->value)
		{	// jpeg
			if (g_piScreenWidth && g_piScreenHeight && g_pGlReadPixels)
			{
				// Get filename
				if (!GetResultsFilename("jpg", filename, fullpath))
				{
					gEngfuncs.Con_Printf("Couldn't construct snapshot filename.\n");
				}
				// Allocate buffer for image data
				int size = *g_piScreenWidth * *g_piScreenHeight * 3;
				uint8_t *pImageData = (uint8_t *)malloc(size);
				if (pImageData)
				{
					// Get image data
					(*g_pGlReadPixels)(0, 0, *g_piScreenWidth, *g_piScreenHeight, GL_RGB, GL_UNSIGNED_BYTE, (void *)pImageData);
					// Compress and save
					jpge::params params;
					params.m_quality = clamp((int)m_pCvarSnapshotJpegQuality->value, 1, 100);
					params.m_subsampling = jpge::H1V1;
					params.m_two_pass_flag = true;
					bool res = jpge::compress_image_to_jpeg_file(fullpath, *g_piScreenWidth, *g_piScreenHeight, 3, pImageData, true, params);
					if (!res)
					{
						gEngfuncs.Con_Printf("Couldn't create snapshot: something bad happen.\n");
					}
					free(pImageData);
				}
				else
				{
					gEngfuncs.Con_Printf("Couldn't allocate buffer for snapshot.\n");
				}
			}
			else
			{
				gEngfuncs.Con_Printf("Can't create snapshot: engine wasn't hooked properly.\n");
			}
		}
		else
		{	// bmp
			if (g_pEngineCreateSnapshot)
			{
				// Get filename
				if (!GetResultsFilename("bmp", filename, fullpath))
				{
					gEngfuncs.Con_Printf("Couldn't construct snapshot filename.\n");
				}
				// Call original snapshot create function, but pass our filename to it
				g_pEngineCreateSnapshot(filename);
			}
			else
			{
				gEngfuncs.Con_Printf("Can't create snapshot: engine wasn't hooked properly.\n");
			}
		}
	}
	else
	{
		// Call original snapshot command handler
		if (g_pEngineSnapshotCommandHandler)
			g_pEngineSnapshotCommandHandler();
		else
			gEngfuncs.Con_Printf("Can't create snapshot: engine wasn't hooked properly.\n");
	}
}

// Our motd_write command block handler
void MotdWriteHandler(void)
{
	gEngfuncs.Con_Printf("motd_write command is blocked on the client to prevent slowhacking.\n");
}

// Our ConnectionlessPacket handler which sanitize redirect packet
void CL_ConnectionlessPacket(void)
{
	if (g_EngineBuf && g_EngineBufSize)
	{
		BEGIN_READ(*g_EngineBuf, *g_EngineBufSize, 0);	// Zero to emulate RESET_READ
		long ffffffff = READ_LONG();
		char *line = READ_LINE();
		char *s = line;
		// Simulate command parsing
		while (*s && *s <= ' ' && *s != '\n')
			s++;
		if (*s == 'L')
		{
			// Redirect packet received. Sanitize it
			s = (char*)*g_EngineBuf;
			s += 5;	// skip FFFFFFFF65
			char *end = (char*)*g_EngineBuf + *g_EngineBufSize;
			while (*s && *s != -1 && *s != ';' && *s != '\n' && s < end)
			{
				s++;
			}
			if (*s == ';')
			{
				s++;
			}
			// Blank out any commands after ';'
			while (*s && *s != -1 && *s != '\n' && s < end)
			{
				*s = ' ';
				s++;
			}
		}
	}
	// Call engine handler
	g_pEngineClConnectionlessPacketHandler();
}

// GameUI copy from console bug fix functions
void __fastcall ConsoleCopyCount(void *pthis, int a, int add)
{
	g_iLinesCounter++;
	g_pFunctionReplacedByCounter(pthis, a, add);
}
void __fastcall ConsoleCopySubst(void *pthis, int a, int pobj, int count)
{
	// This function is called from System009 member at offset 0x0A, we can correct count parameter here and call actial function
	g_pFunctionReplacedBySubst(pthis, a, pobj, count + g_iLinesCounter);
	g_iLinesCounter = 0;
	// Unhook function in System009
	HookDWord((size_t)g_pSystem + 32, (size_t)g_pFunctionReplacedBySubst);
	g_pFunctionReplacedBySubst = 0;
	g_pSystem = 0;
}
size_t __cdecl ConsoleCopyGetSystem(void)
{
	// We are take System009 by ourselvs and replace a function there at offset 0x0A, to be able to correct parameters of a later call
	size_t *pSystem = (size_t *)g_pGet_VGUI_System009();
	g_pSystem = (size_t *)*pSystem;
	g_pFunctionReplacedBySubst = (ThisCallIntInt)HookDWord((size_t)g_pSystem + 32, (size_t)ConsoleCopySubst);
	return (size_t)pSystem;
}

// Micro patches
void FindSvcMessagesTable(void)
{
	if (!g_SvcMessagesTable)
	{
		// Search for "svc_bad" and futher engine messages strings
		size_t svc_bad, svc_nop, svc_disconnect;
		const unsigned char data1[] = "svc_bad";
		svc_bad = MemoryFindForward(g_EngineModuleBase, g_EngineModuleEnd, data1, NULL, sizeof(data1) - 1);
		if (!svc_bad)
		{
			strncat(g_szPatchErrors, "Engine patch: offset of svc_bad not found.\n", sizeof(g_szPatchErrors) - strlen(g_szPatchErrors) - 1);
			return;
		}
		const unsigned char data2[] = "svc_nop";
		svc_nop = MemoryFindForward(svc_bad, g_EngineModuleEnd, data2, NULL, sizeof(data2) - 1);
		if (!svc_nop)
		{
			strncat(g_szPatchErrors, "Engine patch: offset of svc_nop not found.\n", sizeof(g_szPatchErrors) - strlen(g_szPatchErrors) - 1);
			return;
		}
		const unsigned char data3[] = "svc_disconnect";
		svc_disconnect = MemoryFindForward(svc_nop, g_EngineModuleEnd, data3, NULL, sizeof(data3) - 1);
		if (!svc_disconnect)
		{
			strncat(g_szPatchErrors, "Engine patch: offset of svc_disconnect not found.\n", sizeof(g_szPatchErrors) - strlen(g_szPatchErrors) - 1);
			return;
		}

		// Form a pattern to search for engine messages functions table
		unsigned char data4[12 * 3 + 4];
		memset(data4, 0, sizeof(data4));
		*((uint32_t*)data4 + 0) = 0;
		*((uint32_t*)data4 + 1) = svc_bad;
		*((uint32_t*)data4 + 3) = 1;
		*((uint32_t*)data4 + 4) = svc_nop;
		*((uint32_t*)data4 + 6) = 2;
		*((uint32_t*)data4 + 7) = svc_disconnect;
		*((uint32_t*)data4 + 9) = 3;
		const char mask4[] = "FFFFFFFFFFFFFFFF00000000 FFFFFFFFFFFFFFFF00000000 FFFFFFFFFFFFFFFF00000000 FFFFFFFF";
		unsigned char m[MAX_PATTERN];
		ConvertHexString(mask4, m, sizeof(m));
		// We search backward first - table should be there and near
		g_SvcMessagesTable = MemoryFindBackward(svc_bad, g_EngineModuleBase, data4, m, sizeof(data4) - 1);
		if (!g_SvcMessagesTable)
			g_SvcMessagesTable = MemoryFindForward(svc_bad, g_EngineModuleEnd, data4, m, sizeof(data4) - 1);
		if (!g_SvcMessagesTable)
		{
			strncat(g_szPatchErrors, "Engine patch: offset of SvcMessagesTable not found.\n", sizeof(g_szPatchErrors) - strlen(g_szPatchErrors) - 1);
		}
	}
}
void FindEngineMessagesBufferVariables(void)
{
	if (!g_EngineBuf && !g_EngineBufSize && !g_EngineReadPos)
	{
		// Find and get engine messages buffer variables in READ_CHAR function
		const char data1[] = "A1283DCD02 8B1530E67602 8D4801 3BCA 7E0E C7052C3DCD0201000000 83C8FFC3 8B1528E67602 890D283DCD02";
		const char mask1[] = "FF00000000 FFFF00000000 FFFFFF FFFF FF00 FFFF0000000000000000 FFFFFFFF FFFF00000000 FFFF00000000";
		size_t addr1 = MemoryFindForward(g_EngineModuleBase, g_EngineModuleEnd, data1, mask1);
		if (!addr1)
		{
			strncat(g_szPatchErrors, "Engine patch: offsets of EngineBuffer variables (A1283DCD02) not found.\n", sizeof(g_szPatchErrors) - strlen(g_szPatchErrors) - 1);
			return;
		}

		// We have two references of EngineReadPos, compare them
		if ((int *)*(size_t *)(addr1 + 1) != (int *)*(size_t *)(addr1 + 40))
		{
			strncat(g_szPatchErrors, "Engine patch: offsets of EngineReadPos didn't match.\n", sizeof(g_szPatchErrors) - strlen(g_szPatchErrors) - 1);
			return;
		}

		// Pointers to buffer, its size and current buffer read position
		g_EngineBuf = (void **)*(size_t *)(addr1 + 34);
		g_EngineBufSize = (int *)*(size_t *)(addr1 + 7);
		g_EngineReadPos = (int *)*(size_t *)(addr1 + 1);
	}
}
void FindUserMessagesEntry(void)
{
	if (!g_pUserMessages)
	{
		// Search for registered user messages chain entry
		const char data1[] = "81FB00010000 0F8D1B010000 8B3574FF6C03 85F6740B";
		const char mask1[] = "FFFFFFFFFFFF FFFF0000FFFF FFFF00000000 FFFFFF00";
		size_t addr1 = MemoryFindForward(g_EngineModuleBase, g_EngineModuleEnd, data1, mask1);
		if (addr1)
		{
			g_pUserMessages = (UserMessage **)*(size_t *)(addr1 + 14);
		}
		else
		{
			strncat(g_szPatchErrors, "Engine patch: offset of UserMessages entry (81FB000100) not found.\n", sizeof(g_szPatchErrors) - strlen(g_szPatchErrors) - 1);
		}
	}
}
void FindSnapshotAddresses(void)
{
	if (!g_pEngineSnapshotCommandHandler && !g_pEngineCreateSnapshot && !g_pGlReadPixels)
	{
		// Find snapshot addresses
		const char data2[] = "A1B8F26E0481EC8000000085C0741CA1FCFB6C038B80940B000085C0740D8D4C24005150E8";
		const char mask2[] = "FF00000000FFFFFFFFFFFFFFFFFFFFFF00000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF";
		size_t addr2 = MemoryFindForward(g_EngineModuleBase, g_EngineModuleEnd, data2, mask2);
		const char data3[] = "83EC388B44243C5355568B35C036FA03570FAF35BC36FA0368247F690350E8CDDCFEFF8BD833FF83C4083BDF895C244C";
		const char mask3[] = "FFFFFFFFFFFFFFFFFFFFFFFF00000000FFFFFFFF00000000FF00000000FFFF00000000FFFFFFFFFFFFFFFFFFFFFFFFFF";
		size_t addr3 = MemoryFindForward(g_EngineModuleBase, g_EngineModuleEnd, data3, mask3);
		const char data4[] = "83C41485C07520A1C036FA038B0DBC36FA03556801140000680719000050515757FF15A4D5F7038B0D";
		const char mask4[] = "FFFFFFFFFFFFFFFF00000000FFFF00000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF00000000FFFF";
		size_t addr4 = MemoryFindForward(g_EngineModuleBase, g_EngineModuleEnd, data4, mask4);
		// We do splitted checks for addresses to use all we have found
		if (addr2)
		{
			g_pEngineSnapshotCommandHandler = (int (*)(void))addr2;
		}
		else
		{
			strncat(g_szPatchErrors, "Engine patch: offset of EngineSnapshotCommandHandler (A1B8F26E04) not found.\n", sizeof(g_szPatchErrors) - strlen(g_szPatchErrors) - 1);
		}
		if (addr3)
		{
			g_pEngineCreateSnapshot = (int (*)(char *))addr3;
		}
		else
		{
			strncat(g_szPatchErrors, "Engine patch: offset of EngineCreateSnapshot (83EC388B44) not found.\n", sizeof(g_szPatchErrors) - strlen(g_szPatchErrors) - 1);
		}
		if (addr4)
		{
			g_piScreenHeight = (int *)*(size_t *)(addr4 + 8);
			g_piScreenWidth = (int *)*(size_t *)(addr4 + 14);
			g_pGlReadPixels = (int (__stdcall **)(int, int, int, int, DWORD, DWORD, void*))*(size_t *)(addr4 + 35);
		}
		else
		{
			strncat(g_szPatchErrors, "Engine patch: offset of screen parameters (83C41485C0) not found.\n", sizeof(g_szPatchErrors) - strlen(g_szPatchErrors) - 1);
		}
	}
}
void PatchFpsBugPlace(void)
{
	if (!g_FpsBugPlace)
	{
		// Find a place where FPS bug happens
		const char data1[] = "DD052834FA03 DC0DE8986603 83C408 E8D87A1000 89442424DB442424 DD5C242C DD05";
		const char mask1[] = "FFFF00000000 FFFF00000000 FFFFFF FF00000000 FFFFFFFFFFFFFFFF FFFFFFFF FFFF";
		size_t addr1 = MemoryFindForward(g_EngineModuleBase, g_EngineModuleEnd, data1, mask1);
		if (addr1)
		{
			g_FpsBugPlace = addr1;
			g_flFrameTime = (double *)*(size_t *)(addr1 + 2);

			// Patch FPS bug: inject correction function
			const char data2[] = "8D542424 52 50 E8FFFFFFFF 90";
			ConvertHexString(data2, g_FpsBugPlaceBackup, sizeof(g_FpsBugPlaceBackup));
			size_t offset = (size_t)FpsBugFix - (g_FpsBugPlace + 20 + 11);
			*(size_t*)(&(g_FpsBugPlaceBackup[7])) = offset;
			ExchangeMemoryBytes((size_t *)(g_FpsBugPlace + 20), (size_t *)g_FpsBugPlaceBackup, 12);
		}
		else
		{
			strncat(g_szPatchErrors, "Engine patch: offset of FPS bug place (DD052834FA) not found.\n", sizeof(g_szPatchErrors) - strlen(g_szPatchErrors) - 1);
		}
	}
	else
	{
		// Restore FPS engine block
		ExchangeMemoryBytes((size_t *)(g_FpsBugPlace + 20), (size_t *)g_FpsBugPlaceBackup, 12);
		g_FpsBugPlace = 0;
	}
}
void PatchCommandsList(void)
{
	if (!g_pCommandsList)
	{
		// Find commands list entry address
		const char data5[] = "83C4085F5E5B83C410C3 8B35486A0202 85F67417 8B46045053";
		const char mask5[] = "FFFFFFFFFFFFFFFFFFFF FFFF00000000 FFFFFFFF FFFFFFFFFF";
		size_t addr5 = MemoryFindForward(g_EngineModuleBase, g_EngineModuleEnd, data5, mask5);
		if (addr5)
		{
			g_pCommandsList = (CommandLink **)*(size_t *)(addr5 + 12);

			// Hook commands
			CommandLink *cl = *g_pCommandsList;
			while (cl != NULL)
			{
				if (!_stricmp(cl->commandName, "motd_write"))
				{
					g_pOldMotdWriteHandler = (void (*)())HookDWord((size_t)&cl->handler, (uint32_t)MotdWriteHandler);
					break;
				}
				cl = cl->nextCommand;
			}
		}
		else
		{
			strncat(g_szPatchErrors, "Engine patch: offset of CommandsList (83C4085F5E) not found.\n", sizeof(g_szPatchErrors) - strlen(g_szPatchErrors) - 1);
		}
	}
	else
	{
		// Unhook commands
		CommandLink *cl = *g_pCommandsList;
		while (cl != NULL)
		{
			// Search by address cos some commands has invalidated name references due to unloading process
			if (cl->handler == MotdWriteHandler)
			{
				if (!_stricmp(cl->commandName, "motd_write"))
				{
					HookDWord((size_t)&cl->handler, (uint32_t)g_pOldMotdWriteHandler);
					g_pOldMotdWriteHandler = 0;
					break;
				}
			}
			cl = cl->nextCommand;
		}
		g_pCommandsList = 0;
	}
}
void PatchConnectionlessPacketHandler(void)
{
	if (!g_pEngineClConnectionlessPacketHandler)
	{
		// Find CL_ConnectionlessPacket call
		const char data6[] = "833AFF 750A E80DF3FFFF E9";
		const char mask6[] = "FFFFFF FFFF FF00000000 FF";
		size_t addr6 = MemoryFindForward(g_EngineModuleBase, g_EngineModuleEnd, data6, mask6);
		if (addr6)
		{
			g_pEngineClConnectionlessPacketPlace = addr6 + 6;
			size_t offset1 = (size_t)CL_ConnectionlessPacket - (g_pEngineClConnectionlessPacketPlace + 4);
			g_pEngineClConnectionlessPacketOffset = HookDWord(g_pEngineClConnectionlessPacketPlace, offset1);
			g_pEngineClConnectionlessPacketHandler = (void (*)())((g_pEngineClConnectionlessPacketPlace + 4) + g_pEngineClConnectionlessPacketOffset);
		}
		else
		{
			strncat(g_szPatchErrors, "Engine patch: offset of CL_ConnectionlessPacket (833AFF750A) not found.\n", sizeof(g_szPatchErrors) - strlen(g_szPatchErrors) - 1);
		}
	}
	else
	{
		// Restore CL_ConnectionlessPacket call
		HookDWord(g_pEngineClConnectionlessPacketPlace, g_pEngineClConnectionlessPacketOffset);
		g_pEngineClConnectionlessPacketHandler = 0;
		g_pEngineClConnectionlessPacketPlace = 0;
		g_pEngineClConnectionlessPacketOffset = 0;
	}
}
void PatchGameUiConsoleCopy(void)
{
	// Fixes vgui__RichText__CopySelected function to copy all selected chars
	if (!g_pFunctionReplacedByCounter && !g_pGet_VGUI_System009)
	{
		// Find the place where \n is processed
		const char data1[] = "66833C0F0A 752D6A018D4C2420 8BF0E8C6090000 6A0156 8D4C24";
		const char mask1[] = "FFFFFF00FF FFFFFFFFFFFFFFFF FFFFFF00000000 FFFFFF FFFFFF";
		size_t addr1 = MemoryFindForward(g_GameUiModuleBase, g_GameUiModuleEnd, data1, mask1);
		if (!addr1)
		{
			strncat(g_szPatchErrors, "GameUI patch: offset in CopySelected (66833C0F0A) not found.\n", sizeof(g_szPatchErrors) - strlen(g_szPatchErrors) - 1);
			return;
		}

		// Find the place where Get_VGUI_System009 is get before call to its member
		const char data2[] = "508D147152 E8350A0000 83C408E8";
		const char mask2[] = "FFFFFFFFFF FF00000000 FFFFFFFF";
		size_t addr2 = MemoryFindForward(addr1, addr1 + 256, data2, mask2);
		if (!addr2)
		{
			strncat(g_szPatchErrors, "GameUI patch: offset of GetSystem009 (508D147152) not found.\n", sizeof(g_szPatchErrors) - strlen(g_szPatchErrors) - 1);
			return;
		}

		// Inject counting function
		size_t offset1 = (size_t)ConsoleCopyCount - (addr1 + 20);
		g_pFunctionReplacedByCounter = (ThisCallInt)(HookDWord(addr1 + 16, offset1) + addr1 + 20);

		// Inject intercept function
		size_t offset2 = (size_t)ConsoleCopyGetSystem - (addr2 + 18);
		g_pGet_VGUI_System009 = (size_t (*)(void))(HookDWord(addr2 + 14, offset2) + addr2 + 18);
	}
}

// Applies engine patches
void PatchEngine(void)
{
	if (!g_EngineModuleBase) GetEngineModuleAddress();
	if (!g_EngineModuleBase)
	{
		strncat(g_szPatchErrors, "Engine patch: module base not found.\n", sizeof(g_szPatchErrors) - strlen(g_szPatchErrors) - 1);
		return;
	}

	FindSvcMessagesTable();
	FindEngineMessagesBufferVariables();
	FindUserMessagesEntry();
	FindSnapshotAddresses();

	PatchFpsBugPlace();
	PatchCommandsList();
	PatchConnectionlessPacketHandler();
}
// Removes engine patches
void UnPatchEngine(void)
{
	if (!g_EngineModuleBase) return;

	PatchConnectionlessPacketHandler();
	PatchCommandsList();
	PatchFpsBugPlace();

	g_pGlReadPixels = 0;
	g_piScreenWidth = 0;
	g_piScreenHeight = 0;
	g_pEngineCreateSnapshot = 0;
	g_pEngineSnapshotCommandHandler = 0;

	g_pUserMessages = 0;
	g_EngineReadPos = 0;
	g_EngineBufSize = 0;
	g_EngineBuf = 0;
	g_SvcMessagesTable = 0;
}

// Hooks requested SvcMessages functions
void HookSvcMessages(cl_enginemessages_t *pEngineMessages)
{
	// Ensure we have all needed addresses
	if (!g_SvcMessagesTable || !g_EngineBuf || !g_EngineBufSize || !g_EngineReadPos || !g_pUserMessages) return;

	// Do iterate thru registered messages chain and exchange message handlers
	int len = sizeof(cl_enginemessages_t) / 4;
	for (int i = 0; i < len; i++)
	{
		if (((uint32_t *)pEngineMessages)[i] == NULL) continue;
		size_t funcAddr = ((uint32_t *)pEngineMessages)[i];
		size_t addr = g_SvcMessagesTable + i * 12 + 8;
		size_t oldAddr = HookDWord(addr, funcAddr);
		((uint32_t *)pEngineMessages)[i] = oldAddr;
	}
}
// Unhooks requested SvcMessages functions
void UnHookSvcMessages(cl_enginemessages_t *pEngineMessages)
{
	// We just do same exchange for functions addresses
	HookSvcMessages(pEngineMessages);
}

// Applies GameUI patches
void PatchGameUi(void)
{
	if (!g_GameUiModuleBase) GetGameUiModuleAddress();
	if (!g_GameUiModuleBase)
	{
		strncat(g_szPatchErrors, "GameUI patch: module base not found.\n", sizeof(g_szPatchErrors) - strlen(g_szPatchErrors) - 1);
		return;
	}

	PatchGameUiConsoleCopy();
}

// Registers cvars and hooks commands
void MemoryPatcherInit(void)
{
	m_pCvarEngineFixFpsBug = gEngfuncs.pfnRegisterVariable("engine_fix_fpsbug", "1", FCVAR_ARCHIVE);
	m_pCvarEngineSnapshotHook = gEngfuncs.pfnRegisterVariable("engine_snapshot_hook", "1", FCVAR_ARCHIVE);
	m_pCvarSnapshotJpeg = gEngfuncs.pfnRegisterVariable("snapshot_jpeg", "1", FCVAR_ARCHIVE);
	m_pCvarSnapshotJpegQuality = gEngfuncs.pfnRegisterVariable("snapshot_jpeg_quality", "95", FCVAR_ARCHIVE);

	// Hook snapshot command if we were able to find engine handler
	if (g_pEngineSnapshotCommandHandler)
		gEngfuncs.pfnAddCommand("snapshot", SnapshotCmdHandler);

	// Patch GameUI
	PatchGameUi();
}

// Output patch status
void MemoryPatcherHudFrame(void)
{
	if (g_bPatchStatusPrinted) return;

	if (g_szPatchErrors[0] != 0)
	{
		gEngfuncs.pfnConsolePrint("Memory patching failed:\n");
		gEngfuncs.pfnConsolePrint(g_szPatchErrors);
	}

	g_bPatchStatusPrinted = true;
}