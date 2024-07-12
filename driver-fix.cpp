#include <format>
#include <iostream>
#include <windows.h>
#include <SetupAPI.h>
#include <initguid.h>

// network adapter GUID
DEFINE_GUID(GUID_DEVCLASS_NET,
	0x4d36e972, 0xe325, 0x11ce, 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18);

bool run_command(const std::string& command, const std::string& args)
{
	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	ZeroMemory(&si, sizeof(si));
	ZeroMemory(&pi, sizeof(pi));

	si.cb = sizeof(si);

	std::string cmd_line = command + " " + args;

	int success = CreateProcess(command.c_str(), cmd_line.data(), nullptr, nullptr, false, 0, nullptr, nullptr, &si, &pi);
	if (!success)
	{
		std::cerr << "[!] Error creating process: " << std::system_category().message(GetLastError()) << '\n';
		return false;
	}

	WaitForSingleObject(pi.hProcess, INFINITE);
	return true;
}	

bool reset_network_stack()
{
	// get the path to the command prompt
	char* buf = nullptr;
	size_t buf_size = 0;

	errno_t err = _dupenv_s(&buf, &buf_size, "COMSPEC");
	if (err || buf == nullptr)
	{
		std::cerr << "[!] Error: COMSPEC environment variable not found.\n";
		return false;
	}

	const std::string command = buf;
	const char drive_letter = buf[0];

	free(buf);

	if (!run_command(command, std::format(R"(/C {}:\Windows\System32\netsh.exe int ip reset)", drive_letter)))
		return false;
	
	if (!run_command(command, std::format(R"(/C {}:\Windows\System32\netsh.exe int ipv6 reset)", drive_letter)))
		return false;
	
	if (!run_command(command, std::format(R"(/C {}:\Windows\System32\netsh.exe winsock reset)", drive_letter)))
		return false;

	return true;
}

int main()
{
	std::cout << "[+] Resetting network stack...\n\n";

	if (!reset_network_stack())
	{
		std::cerr << "[!] Failed to reset network stack.\n";
		std::cin.get();

		return 1;
	}

	// grab all devices from the network adapter
	const HDEVINFO device_info_set = SetupDiGetClassDevsW(&GUID_DEVCLASS_NET, nullptr, nullptr, DIGCF_PRESENT);

	SP_DEVINFO_DATA device_info_data;
	device_info_data.cbSize = sizeof(SP_DEVINFO_DATA);

	int member_index = 0;
	int wan_miniport_count = 0;

	std::cout << "[+] Removing WAN miniports...\n";

	// iterate through all devices
	while (SetupDiEnumDeviceInfo(device_info_set, member_index, &device_info_data))
	{
		DWORD data_type;
		DWORD required_size;
		BYTE buffer[4096];

		// get the friendly name of the device
		if (SetupDiGetDeviceRegistryPropertyW(device_info_set, &device_info_data, SPDRP_FRIENDLYNAME, &data_type, buffer,
		                                     sizeof(buffer), &required_size))
		{
			// ensure the buffer contains a null-terminated string
			buffer[sizeof(buffer) - 1] = 0;

			// check if the device is a WAN miniport
			if (wcsstr(reinterpret_cast<wchar_t*>(buffer), L"WAN Miniport"))
			{
				// remove the device
				if (!SetupDiRemoveDevice(device_info_set, &device_info_data))
				{
					std::cerr << "[!] Error removing device: " << std::system_category().message(GetLastError()) << '\n';
					continue;
				}

				std::wcout << "\n" << reinterpret_cast<wchar_t*>(buffer) << " was removed";
				wan_miniport_count++;
			}
		}
		else if (SetupDiGetDeviceRegistryPropertyW(device_info_set, &device_info_data, SPDRP_DEVICEDESC, &data_type, buffer,
			sizeof(buffer), &required_size))
		{
			// ensure the buffer contains a null-terminated string
			buffer[sizeof(buffer) - 1] = 0;

			// check if the device is a WAN miniport
			if (wcsstr(reinterpret_cast<wchar_t*>(buffer), L"WAN Miniport"))
			{
				// remove the device
				if (!SetupDiRemoveDevice(device_info_set, &device_info_data))
				{
					std::cerr << "[!] Error removing device: " << std::system_category().message(GetLastError()) << '\n';
					continue;
				}

				std::wcout << "\n" << reinterpret_cast<wchar_t*>(buffer) << " was removed";
				wan_miniport_count++;
			}
		}
		else
		{
			std::cout << "[!] Error getting device property: " << std::system_category().message(GetLastError()) << '\n';
		}

		member_index++;
	}

	const DWORD error = GetLastError();
	if (error == ERROR_NO_MORE_ITEMS)
	{
		// no miniports were found
		if (!wan_miniport_count)
		{
			std::cerr << "[!] No WAN miniports were found, please restart your computer.\n\n";
			std::cin.get();

			return 1;
		}

		// success
		std::cout << "\n[+] WAN miniports removed\n\n";
		std::cout << "Finished, please restart your computer for the changes to take effect.";

		std::cin.get();

		return 0;
	}

	std::cerr << "[!] Error: " << std::system_category().message(error) << "\n\n";
	std::cin.get();

	return 1;
}