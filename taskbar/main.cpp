#include <iostream>
#include "MinHook.h"

int main()
{
	// initalise minhook
	std::cout << "Initalising minhook...";
	if (MH_Initialize() != MH_OK)
	{
		std::cout << "failed." << std::endl;
		return -1;
	}
	std::cout << "ok." << std::endl;

	// todo: do work
	std::cout << "todo: work" << std::endl;

	// clean up
	std::cout << "Uninitalising minhook...";
	if (MH_Uninitialize() != MH_OK)
	{
		std::cout << "failed." << std::endl;
		return -1;
	}
	std::cout << "ok." << std::endl;

	return 0;
}
