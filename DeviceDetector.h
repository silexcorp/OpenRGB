#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ResourceManager.h"

class DeviceDetector
{
public:
    DeviceDetector(std::string name, DeviceDetectorFunction detector)
	{
        ResourceManager::get()->RegisterDeviceDetector(name, detector);
	}
};

class I2CDeviceDetector
{
public:
    I2CDeviceDetector(std::string name, I2CDeviceDetectorFunction detector)
	{
        ResourceManager::get()->RegisterI2CDeviceDetector(name, detector);
	}
};

class I2CBusDetector
{
public:
    I2CBusDetector(I2CBusDetectorFunction detector)
    {
        ResourceManager::get()->RegisterI2CBusDetector(detector);
    }
};
