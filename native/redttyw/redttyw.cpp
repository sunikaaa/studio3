// redttyw.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

static BOOL InjectHookDLL(HANDLE hProcess);
static BOOL ShareHandles(HANDLE hProcess);

static HANDLE hMapFile = NULL;

int _tmain(int argc, _TCHAR* argv[])
{
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	DWORD dwFlags = 0;
	DWORD dwResult = 0;

	HANDLE hStdInR = NULL, hStdInW = NULL;
	HANDLE hStdOutR = NULL, hStdOutW = NULL;
	HANDLE hStdErrR = NULL, hStdErrW = NULL;
	HANDLE hParentStdIn = ::GetStdHandle(STD_INPUT_HANDLE);
	HANDLE hParentStdOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
	HANDLE hParentStdErr = ::GetStdHandle(STD_ERROR_HANDLE);
	TCHAR szCmdline[1024];

	if( argc < 2) {
		return -1;
	}
	_tcscpy_s(szCmdline, sizeof(szCmdline)/sizeof(*szCmdline), argv[1]);

	//_tcscpy_s(szCmdline, sizeof(szCmdline)/sizeof(*szCmdline), _T("C:\\WINDOWS\\system32\\cmd.exe /u /c \"\"C:\\Program Files\\Git\\bin\\sh.exe\"  --login -i\""));
	//_tcscpy_s(szCmdline, sizeof(szCmdline)/sizeof(*szCmdline), _T("\"C:\\Program Files\\Git\\bin\\sh.exe\"  --login -i"));

	BOOL bIsTTY = (::GetFileType(hParentStdIn) == FILE_TYPE_CHAR)
					|| (::GetFileType(hParentStdOut) == FILE_TYPE_CHAR)
					|| (::GetFileType(hParentStdErr) == FILE_TYPE_CHAR);
	
	if ( bIsTTY )
	{
		SECURITY_ATTRIBUTES sa;
		sa.nLength = sizeof(SECURITY_ATTRIBUTES); 
		sa.bInheritHandle = TRUE; 
		sa.lpSecurityDescriptor = NULL;

		// STDOUT
		if ( !::CreatePipe(&hStdOutR, &hStdOutW, &sa, 0) ) {
			printf("Cannot create pipe (%d)\n", GetLastError());
			return -1;
		}
		if ( !::SetHandleInformation(hStdOutR, HANDLE_FLAG_INHERIT, 0) ) {
			printf("Set inheritable for pipe failed (%d)\n", GetLastError());
			return -1;
		}
		// STDERR
		if ( !::CreatePipe(&hStdErrR, &hStdErrW, &sa, 0) ) {
			printf("Cannot create pipe (%d)\n", GetLastError());
			return -1;
		}
		if ( !::SetHandleInformation(hStdErrR, HANDLE_FLAG_INHERIT, 0) ) {
			printf("Set inheritable for pipe failed (%d)\n", GetLastError());
			return -1;
		}
		// STDIN
		if ( !::CreatePipe(&hStdInR, &hStdInW, &sa, 0) ) {
			printf("Cannot create pipe (%d)\n", GetLastError());
			return -1;
		}
		if ( !::SetHandleInformation(hStdInW, HANDLE_FLAG_INHERIT, 0) ) {
			printf("Set inheritable for pipe failed (%d)\n", GetLastError());
			return -1;
		}

		::SetStdHandle(STD_INPUT_HANDLE, hStdInR);
		::SetStdHandle(STD_OUTPUT_HANDLE, hStdOutW);
		::SetStdHandle(STD_ERROR_HANDLE, hStdErrW);
	}

	::ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_FORCEOFFFEEDBACK | STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	si.lpTitle = _T("WINTTY");
	if( argc > 2 ) {
		int width, height;
		if( _stscanf_s(argv[2], _T("%dx%d"), &width, &height) == 2 ) {
			si.dwXCountChars = width;
			si.dwYCountChars = height;
			si.dwFlags |= STARTF_USECOUNTCHARS;
		}
	}
	if( (argc > 3) && _tcscmp(argv[3], _T("-show")) == 0 ) {
		si.dwFlags &= ~STARTF_USESHOWWINDOW;
	}
	::ZeroMemory(&pi, sizeof(pi));

#ifdef UNICODE
    dwFlags = CREATE_UNICODE_ENVIRONMENT;
#endif

	::SetEnvironmentVariable(_T("TERM"), _T("xterm-color"));

	if( !::CreateProcess(
		NULL,		// No module name (use command line)
        szCmdline,	// Command line
        NULL,		// Process handle not inheritable
        NULL,		// Thread handle not inheritable
        FALSE,		// Set handle inheritance
		dwFlags | CREATE_NEW_CONSOLE | CREATE_SUSPENDED,	// creation flags
        NULL,           // Use parent's environment block
        NULL,           // Use parent's starting directory 
        &si,            // Pointer to STARTUPINFO structure
        &pi )           // Pointer to PROCESS_INFORMATION structure
		) {
        printf( "CreateProcess failed (%d).\n", GetLastError() );
        return -1;
	}

	if( ShareHandles(pi.hProcess) && InjectHookDLL(pi.hProcess) ) {
		::ResumeThread(pi.hThread);
		::CloseHandle(pi.hThread);
	} else {
		::TerminateProcess(pi.hProcess, EXIT_SUCCESS);
	}

	if( bIsTTY )
	{
		CHAR chBuf[1024];
		while( true ) {
			DWORD dwRead = 0, dwWritten = 0;
			DWORD dwExitCode = ::WaitForSingleObject(pi.hProcess, 100);
			::FlushFileBuffers(hParentStdIn);
			while ( ::PeekNamedPipe(hParentStdIn, NULL, 0L, NULL, &dwRead, NULL) && (dwRead != 0) ) {
				if( !::ReadFile(hParentStdIn, chBuf, sizeof(chBuf), &dwRead, NULL) ) {
					break;
				}
				if( !::WriteFile(hStdInW, chBuf, dwRead, &dwWritten, NULL) ) {
					break;
				}
			}
			if ( dwWritten != 0 ) {
				continue;
			}
			::FlushFileBuffers(hStdOutR);
			while ( ::PeekNamedPipe(hStdOutR, NULL, 0L, NULL, &dwRead, NULL) && (dwRead != 0) ) {
				if( !::ReadFile(hStdOutR, chBuf, sizeof(chBuf), &dwRead, NULL) ) {
					break;
				}
				if( !::WriteFile(hParentStdOut, chBuf, dwRead, &dwWritten, NULL) ) {
					break;
				}
			}
			if ( dwWritten != 0 ) {
				continue;
			}
			::FlushFileBuffers(hStdErrR);
			while ( ::PeekNamedPipe(hStdErrR, NULL, 0L, NULL, &dwRead, NULL) && (dwRead != 0) ) {
				if( !::ReadFile(hStdErrR, chBuf, sizeof(chBuf), &dwRead, NULL) ) {
					break;
				}
				if( !::WriteFile(hParentStdErr, chBuf, dwRead, &dwWritten, NULL) ) {
					break;
				}
			}
			if ( dwWritten != 0 ) {
				continue;
			}
			if( dwExitCode != WAIT_TIMEOUT ) {
				break;
			}
		}
		::CloseHandle(hStdInW);
		::CloseHandle(hStdOutR);
		::CloseHandle(hStdErrR);
	}

	WaitForSingleObject(pi.hProcess, INFINITE);
	if( hMapFile != NULL ) {
		::CloseHandle(hMapFile);
	}

	::CloseHandle(pi.hProcess);
	return 0;
}

static BOOL InjectHookDLL(HANDLE hProcess)
{
	TCHAR szPath[MAX_PATH+1];
	BOOL bResult = FALSE;
	LPVOID lpHookDllPathRemote;
	::ZeroMemory(szPath, sizeof(szPath));
#ifdef _DEBUG
	_tcscpy_s(szPath, sizeof(szPath)/sizeof(*szPath), _T("C:\\studio\\gitworkspaces\\red_core\\native\\wintty\\Debug\\"));
#else
	if( ::GetModuleFileName(NULL, szPath, MAX_PATH) == MAX_PATH ) {
		return FALSE;
	}
#endif
	*_tcsrchr(szPath, _T('\\')) = 0;
	_tcscat_s(szPath, sizeof(szPath)/sizeof(*szPath), _T("\\wintty.dll"));

	lpHookDllPathRemote = ::VirtualAllocEx(hProcess, NULL, sizeof(szPath), MEM_COMMIT, PAGE_READWRITE);
	if( lpHookDllPathRemote == NULL ) {
		return FALSE;
	}
	if( ::WriteProcessMemory(hProcess, lpHookDllPathRemote, szPath, sizeof(szPath), NULL) )
	{
		PTHREAD_START_ROUTINE pfnThreadRoutine = (PTHREAD_START_ROUTINE)::GetProcAddress(::GetModuleHandle(_T("Kernel32.dll")), "LoadLibraryW");
		if( pfnThreadRoutine != NULL ) {
			HANDLE hRemoteThread = ::CreateRemoteThread(hProcess, NULL, 0, pfnThreadRoutine, lpHookDllPathRemote, 0, NULL);
			if( hRemoteThread != NULL ) {
				bResult = (::WaitForSingleObject(hRemoteThread, 10000) == WAIT_OBJECT_0);
				::CloseHandle(hRemoteThread);
			}
		}
	}
	::VirtualFreeEx(hProcess, lpHookDllPathRemote, 0, MEM_RELEASE);
	return bResult;
}

static BOOL ShareHandles(HANDLE hProcess)
{
	TCHAR szName[64];
	_ui64tot_s(::GetProcessId(hProcess), szName, sizeof(szName)/sizeof(szName[0]), 16);
	_tcscat_s(szName, sizeof(szName)/sizeof(szName[0]), _T("-WINTTY"));
	DWORD dwSize = 3*sizeof(HANDLE);
	hMapFile = ::CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE | SEC_COMMIT, 0, dwSize, szName);
	if( hMapFile != NULL )
	{
		LPHANDLE lpData = (LPHANDLE)::MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, dwSize);
		if( lpData != NULL )
		{
			lpData[0] = ::GetStdHandle(STD_INPUT_HANDLE);
			lpData[1] = ::GetStdHandle(STD_OUTPUT_HANDLE);
			lpData[2] = ::GetStdHandle(STD_ERROR_HANDLE);
			::UnmapViewOfFile(lpData);
			return TRUE;
		}
	}
	return FALSE;
}