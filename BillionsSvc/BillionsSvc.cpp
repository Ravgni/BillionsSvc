#include <windows.h>
#include <tchar.h>
#include <strsafe.h>
#include <Psapi.h>
#include <fstream>
#include <future>
#include <filesystem>
#include <set>
#include "contrib/minizip/zip.h"

const WCHAR* SvcName = TEXT("BillionsSvc");
const WCHAR* ProcName = TEXT("TheyAreBillions.exe");

SERVICE_STATUS          gSvcStatus;
SERVICE_STATUS_HANDLE   gSvcStatusHandle;

void SvcInstall(SC_HANDLE&);
void WINAPI SvcCtrlHandler(DWORD);
void WINAPI SvcMain();

void ReportSvcStatus(DWORD, DWORD, DWORD);
void SvcInit();

bool CheckIfProcessorRunning();
void job();

std::string timeToString(const std::filesystem::file_time_type& time);
time_t stringToTime(const std::string& time);

std::promise<void> gStopPromise;
std::future<void> gStopFuture = gStopPromise.get_future();
uint8_t gbackupsSize = 5;

//
// Purpose: 
//   Entry point for the processprocess
//
// Parameters:
//   None
// 
// Return value:
//   None, defaults to 0 (zero)
//
int __cdecl _tmain(int argc, char* argv[]) {
	// If command-line parameter is "install", install the service. 
	// Otherwise, the service is probably being started by the SCM.
	// Get a handle to the SCM database. 
	while (!IsDebuggerPresent()) {
		Sleep(1000);
	}

	if (argc > 1) {
		int temp = atoi(argv[1]);
		gbackupsSize = temp > UINT8_MAX ? UINT8_MAX : static_cast<uint8_t>(temp);
	}

	job();

	SC_HANDLE schSCManager;

	schSCManager = OpenSCManager(
		NULL,                    // local computer
		NULL,                    // servicesActive database 
		SC_MANAGER_ALL_ACCESS);  // full access rights 

	if (NULL == schSCManager) {
		printf("OpenSCManager failed (%ld)\n", GetLastError());
		return 0;
	}

	// Get a handle to the service.
	SC_HANDLE schService;

	schService = OpenService(
		schSCManager,         // SCM database 
		SvcName,            // name of service 
		SERVICE_ALL_ACCESS);  // full access 

	if (schService == NULL) {
		DWORD err = GetLastError();
		if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
			SvcInstall(schSCManager);
		}
		else {
			printf("OpenService failed (%ld)\n", err);
			CloseServiceHandle(schSCManager);
		}
		return 0;
	}

	SERVICE_TABLE_ENTRY DispatchTable[] =
	{
		{ const_cast<LPWSTR>(SvcName), (LPSERVICE_MAIN_FUNCTION)SvcMain },
		{ NULL, NULL }
	};

	// This call returns when the service has stopped. 
	// The process should simply terminate when the call returns.

	StartServiceCtrlDispatcher(DispatchTable);

	CloseServiceHandle(schService);
	CloseServiceHandle(schSCManager);

	return 0;
}


//
// Purpose: 
//   Installs a service in the SCM database 
//
// Parameters:
//   None
// 
// Return value:
//   None
//
void SvcInstall(SC_HANDLE& schSCManager) {
	SC_HANDLE schService;
	TCHAR szUnquotedPath[MAX_PATH];

	if (!GetModuleFileName(NULL, szUnquotedPath, MAX_PATH)) {
		printf("Cannot install service (%ld)\n", GetLastError());
		return;
	}

	// In case the path contains a space, it must be quoted so that
	// it is correctly interpreted. For example,
	// "d:\my share\myservice.exe" should be specified as
	// ""d:\my share\myservice.exe"".
	TCHAR szPath[MAX_PATH];
	StringCbPrintf(szPath, MAX_PATH, TEXT("\"%s\""), szUnquotedPath);

	// Create the service

	schService = CreateService(
		schSCManager,              // SCM database 
		SvcName,                   // name of service 
		SvcName,                   // service name to display 
		SERVICE_ALL_ACCESS,        // desired access 
		SERVICE_WIN32_OWN_PROCESS, // service type 
		SERVICE_AUTO_START,      // start type 
		SERVICE_ERROR_NORMAL,      // error control type 
		szPath,                    // path to service's binary 
		NULL,                      // no load ordering group 
		NULL,                      // no tag identifier 
		NULL,                      // no dependencies 
		NULL,                      // LocalSystem account 
		NULL);                     // no password 

	if (schService == NULL) {
		printf("CreateService failed (%ld)\n", GetLastError());
		CloseServiceHandle(schSCManager);
		return;
	}
	else printf("Service installed successfully\n");
}

//
// Purpose: 
//   Entry point for the service
//
// Parameters:
//   dwArgc - Number of arguments in the lpszArgv array
//   lpszArgv - Array of strings. The first string is the name of
//     the service and subsequent strings are passed by the process
//     that called the StartService function to start the service.
// 
// Return value:
//   None.
//
void WINAPI SvcMain() {
	// Register the handler function for the service

	gSvcStatusHandle = RegisterServiceCtrlHandler(
		SvcName,
		SvcCtrlHandler);

	if (!gSvcStatusHandle) {
		return;
	}

	// These SERVICE_STATUS members remain as set here

	gSvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	gSvcStatus.dwServiceSpecificExitCode = 0;

	// Report initial status to the SCM

	ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

	// Perform service-specific initialization and work.

	SvcInit();
}

//
// Purpose: 
//   The service code
//
// Parameters:
//   dwArgc - Number of arguments in the lpszArgv array
//   lpszArgv - Array of strings. The first string is the name of
//     the service and subsequent strings are passed by the process
//     that called the StartService function to start the service.
// 
// Return value:
//   None
//
void SvcInit() {
	// TO_DO: Declare and set any required variables.
	//   Be sure to periodically call ReportSvcStatus() with 
	//   SERVICE_START_PENDING. If initialization fails, call
	//   ReportSvcStatus with SERVICE_STOPPED.

	// Report running status when initialization is complete.

	ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);

	// Check whether to stop the service.
	std::thread t(job);

	gStopFuture.wait();
	gStopFuture.get();
	t.join();

	ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
	return;
}

//
// Purpose: 
//   Sets the current service status and reports it to the SCM.
//
// Parameters:
//   dwCurrentState - The current state (see SERVICE_STATUS)
//   dwWin32ExitCode - The system error code
//   dwWaitHint - Estimated time for pending operation, 
//     in milliseconds
// 
// Return value:
//   None
//
void ReportSvcStatus(DWORD dwCurrentState,
	DWORD dwWin32ExitCode,
	DWORD dwWaitHint) {
	static DWORD dwCheckPoint = 1;

	// Fill in the SERVICE_STATUS structure.

	gSvcStatus.dwCurrentState = dwCurrentState;
	gSvcStatus.dwWin32ExitCode = dwWin32ExitCode;
	gSvcStatus.dwWaitHint = dwWaitHint;

	if (dwCurrentState == SERVICE_START_PENDING)
		gSvcStatus.dwControlsAccepted = 0;
	else gSvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;

	if ((dwCurrentState == SERVICE_RUNNING) ||
		(dwCurrentState == SERVICE_STOPPED))
		gSvcStatus.dwCheckPoint = 0;
	else gSvcStatus.dwCheckPoint = dwCheckPoint++;

	// Report the status of the service to the SCM.
	SetServiceStatus(gSvcStatusHandle, &gSvcStatus);
}

//
// Purpose: 
//   Called by SCM whenever a control code is sent to the service
//   using the ControlService function.
//
// Parameters:
//   dwCtrl - control code
// 
// Return value:
//   None
//
void WINAPI SvcCtrlHandler(DWORD dwCtrl) {
	// Handle the requested control code. 

	switch (dwCtrl) {
		case SERVICE_CONTROL_STOP:
			ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);

			// Signal the service to stop.

			gStopPromise.set_value();

			return;

		default:
			break;
	}

}

bool CheckIfProcessorRunning() {
	TCHAR szProcessName[MAX_PATH];
	DWORD aProcesses[512], cbNeeded = 0;
	HMODULE hMod;

	EnumProcesses(aProcesses, sizeof(aProcesses), &cbNeeded);
	const DWORD szProc = cbNeeded / sizeof(DWORD);

	for (unsigned int i = 0; i < szProc; ++i) {
		if (HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, aProcesses[i])) {
			if (EnumProcessModulesEx(hProcess, &hMod, sizeof(hMod), &cbNeeded, 0)) {
				GetModuleBaseName(hProcess, hMod, szProcessName, sizeof(szProcessName) / sizeof(TCHAR));
				if (wcscmp(szProcessName, ProcName) == 0) {
					CloseHandle(hProcess);
					return true;
				}
			}

			CloseHandle(hProcess);
		}
	}

	return false;
}

struct cmdPr {
	bool operator()(const std::string& Left, const std::string& Right) const {
		return stringToTime(Left) < stringToTime(Right);
	}
};

void job() {
	namespace fs = std::filesystem;

	WCHAR* envBuf = (LPWSTR)malloc(MAX_PATH * sizeof(WCHAR));
	const WCHAR* SavePath = GetEnvironmentVariable(L"billionspath", envBuf, MAX_PATH) != 0 ? envBuf : TEXT("C:\\Users\\igogo\\Document s\\My Games\\They Are Billions\\Saves");
	free(envBuf);

	const fs::path saveDir(SavePath), backupDir = saveDir.parent_path();
	fs::directory_iterator backupIt(backupDir);
	fs::file_time_type tWrite;
	std::set<std::string, cmdPr> sBackups;
	std::set<fs::path> backupFiles;
	const std::string backupPrefix = backupDir.string() + "//backup_";
	std::string backupPath;
	std::fstream f;

	while (!backupIt._At_end()) {
		if (auto& entry = *backupIt; entry.is_regular_file() && entry.path().extension() == ".zip") {
			if (std::string backupName = entry.path().filename().string(); backupName.starts_with("backup_")) {
				if (sBackups.size() >= gbackupsSize) {
					if (stringToTime(backupName) > stringToTime(*sBackups.begin())) {
						sBackups.erase(sBackups.begin());
					}
					else {
						continue;
					}
				}
				sBackups.emplace(entry.path().string());
			}
		}
		backupIt++;
	}

	while (gStopFuture.valid()) {
		if (CheckIfProcessorRunning()) {
			bool write = false;
			fs::directory_iterator savIt(saveDir);

			while (!savIt._At_end()) {
				const auto& entry = *savIt;
				if (!backupFiles.contains(entry.path())) {
					backupFiles.emplace(entry.path());
				}

				if (entry.is_regular_file() && entry.path().extension() == ".zxsav") {
					if (const auto t = entry.last_write_time(); t > tWrite) {
						tWrite = t;
						write = true;
					}
				}
				savIt++;
			}
			if (write) {
				if (sBackups.size() >= gbackupsSize) {
					fs::remove(*sBackups.begin());
					sBackups.erase(sBackups.begin());
				}

				backupPath = backupPrefix + timeToString(tWrite) + ".zip";
				const std::streamsize bufSz = 1 << 14;
				unsigned int readSz = 0;
				char* buf = (char*)malloc(bufSz);
				zipFile zf = zipOpen(backupPath.c_str(), 0);

				for (auto& Save : backupFiles) {
					f.open(Save.string(), std::ios::in | std::ios::binary);
					if (f.is_open()) {
						zipOpenNewFileInZip(zf, Save.filename().string().c_str(), NULL, NULL, 0, NULL, 0, NULL, 0, Z_NO_COMPRESSION);

						do {
							f.read(buf, bufSz);
							readSz = static_cast<unsigned int>(f.gcount());
							zipWriteInFileInZip(zf, buf, readSz);

						} while (readSz == bufSz);

						zipCloseFileInZip(zf);
						f.close();
					}
					else {
						backupFiles.erase(Save);
					}
				}
				zipClose(zf, NULL);

				sBackups.emplace(std::move(backupPath));
				write = false;
				free(buf);
			}

		}
		Sleep(3000);
	}
}

std::string timeToString(const std::filesystem::file_time_type& time) {
	using namespace std::chrono;
	zoned_time zTime(current_zone(), utc_clock::to_sys(file_clock::to_utc(time)));
	return std::format("{:%H-%M-%OS_%d-%m-%y}", zTime);
}

time_t stringToTime(const std::string& time) {
	tm tm = {};
	tm.tm_hour = atoi(time.substr(7, 2).c_str());
	tm.tm_min = atoi(time.substr(10, 2).c_str());
	tm.tm_sec = atoi(time.substr(13, 2).c_str());
	tm.tm_mday = atoi(time.substr(16, 2).c_str());
	tm.tm_mon = atoi(time.substr(19, 2).c_str()) - 1;
	tm.tm_year = atoi(time.substr(22, 2).c_str()) + 100;

	return mktime(&tm);
}