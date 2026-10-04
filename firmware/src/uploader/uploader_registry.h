//
// Created by Nicholas Wiersma on 2026/08/20.
//

#pragma once

#include "config.h"

// syncUploaderInstances reconciles the configured uploader list
// (uploaderConfigs, written by config.cpp from config.json) with the live
// Uploader instances actually running, creating, reconfiguring, or stopping
// instances as needed. It is the only code that creates or stops Uploader
// instances.
//
// Normally it only does work when uploadersChanged is set (cheap steady
// state ticks); pass force=true to make it (re)run regardless, which
// main.cpp does once at boot to instantiate uploaders from the freshly
// loaded config.
void syncUploaderInstances(bool force = false);

// syncUploaders is the Core0 task wrapper around syncUploaderInstances().
uint32_t syncUploaders(void *param);
