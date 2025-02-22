#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include "shared_data.h"

void* storage_manager(void* arg);
void insert_sensor_data(SharedData* shared, int sensor_id, double temperature, double humidity);

#endif // STORAGE_MANAGER_H