#include <windows.h>
#include <iostream>
#include <iomanip>

int main() {
    // Attempt to find the Registry Editor window
    HWND hwnd = FindWindowW(L"RegEdit_RegEdit", NULL);

    if (hwnd == NULL) {
        std::cout << "[!] Target window (Registry Editor) not found." << std::endl;
        std::cout << "    Please ensure Registry Editor is running and try again." << std::endl;
    }
    else {
        std::cout << "[+] Target window found successfully!" << std::endl;
        std::cout << "---------------------------------------" << std::endl;

        // Display the handle in hexadecimal format (to be used in the kernel driver)
        std::cout << "Target HWND: 0x"
            << std::uppercase << std::hex << (ULONG_PTR)hwnd << std::endl;

        std::cout << "---------------------------------------" << std::endl;
        std::cout << "[*] Copy this HWND into your driver's DriverEntry for testing." << std::endl;
    }

    std::cout << "\nPress Enter to exit..." << std::endl;
    std::cin.ignore();
    std::cin.get();
    return 0;
}